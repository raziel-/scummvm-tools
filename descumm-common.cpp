/* DeScumm - Scumm Script Disassembler (common code)
 * Copyright (C) 2001  Ludvig Strigeus
 * Copyright (C) 2002-2003  The ScummVM Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * $Header$
 *
 */

#include "descumm.h"


BlockStack *block_stack;
int num_block_stack;

bool pendingElse, haveElse;
int pendingElseTo;
int pendingElseOffs;
int pendingElseOpcode;
int pendingElseIndent;

int g_jump_opcode;

bool alwaysShowOffs = false;
bool dontOutputIfs = false;
bool dontOutputElse = false;
bool dontOutputElseif = false;
bool dontOutputWhile = false;
bool dontShowOpcode = false;
bool dontShowOffsets = false;
bool haltOnError;

byte scriptVersion;

byte *cur_pos, *org_pos;
int offs_of_line;

uint size_of_code;


///////////////////////////////////////////////////////////////////////////

char *strecpy(char *buf, const char *src)
{
	strcpy(buf, src);
	return strchr(buf, 0);
}

int get_curoffs()
{
	return cur_pos - org_pos;
}

int get_byte()
{
	return (byte)(*cur_pos++);
}

uint get_word()
{
	int i;

	if (scriptVersion == 8) {
		i = TO_LE_32(*((int32 *)cur_pos));
		cur_pos += 4;
	} else {
		i = TO_LE_16(*((int16 *)cur_pos));
		cur_pos += 2;
	}
	return i;
}

int get_signed_word()
{
	uint i = get_word();

	if (scriptVersion == 8) {
		return (int32)i;
	} else {
		return (int16)i;
	}
}


///////////////////////////////////////////////////////////////////////////

#define INDENT_SIZE 2

static char *indentbuf;

char *getIndentString(int i)
{
	char *c = indentbuf;
	i += i;
	if (!c)
		indentbuf = c = (char *)malloc(127 * INDENT_SIZE + 1);
	if (i >= 127 * INDENT_SIZE)
		i = 127 * INDENT_SIZE;
	if (i < 0)
		i = 0;
	memset(c, 32, i);
	c[i] = 0;
	return c;
}

void outputLine(char *buf, int curoffs, int opcode, int indent)
{
	char *s;

	if (buf[0]) {
		if (indent == -1)
			indent = num_block_stack;
		if (curoffs == -1)
			curoffs = get_curoffs();

		s = getIndentString(indent);

		if (dontShowOpcode) {
			if (dontShowOffsets)
				printf("%s%s\n", s, buf);
			else
				printf("[%.4X] %s%s\n", curoffs, s, buf);
		} else {
			char buf2[4];
			if (opcode != -1)
				sprintf(buf2, "%.2X", opcode);
			else
				strcpy(buf2, "**");
			if (dontShowOffsets)
				printf("(%s) %s%s\n", buf2, s, buf);
			else
				printf("[%.4X] (%s) %s%s\n", curoffs, buf2, s, buf);
		}
	}
}

///////////////////////////////////////////////////////////////////////////

bool indentBlock(unsigned int cur)
{
	BlockStack *p;

	if (!num_block_stack)
		return false;

	p = &block_stack[num_block_stack - 1];
	if (cur < p->to)
		return false;

	num_block_stack--;
	return true;
}


BlockStack *pushBlockStackItem()
{
	if (!block_stack)
		block_stack = (BlockStack *) malloc(256 * sizeof(BlockStack));

	if (num_block_stack >= 256) {
		printf("block_stack full!\n");
		exit(0);
	}
	return &block_stack[num_block_stack++];
}

// Returns 0 or 1 depending if it's ok to add a block
bool maybeAddIf(uint cur, uint to)
{
	int i;
	BlockStack *p;
	
	if (((to | cur) >> 16) || (to <= cur))
		return false; // Invalid jump
	
	for (i = 0, p = block_stack; i < num_block_stack; i++, p++) {
		if (to > p->to)
			return false;
	}
	
	p = pushBlockStackItem();

	// Try to determine if this is a while loop. For this, first check if we 
	// jump right behind a regular jump, then whether that jump is targeting us.
	if (scriptVersion == 8) {
		p->isWhile = (*(byte*)(org_pos+to-5) == g_jump_opcode);
		i = (int32)TO_LE_32(*(int32*)(org_pos+to-4));
	} else {
		p->isWhile = (*(byte*)(org_pos+to-3) == g_jump_opcode);
		i = (int16)TO_LE_16(*(int16*)(org_pos+to-2));
	}
	
	p->isWhile = p->isWhile && (offs_of_line == (int)to + i);
	p->from = cur;
	p->to = to;
	return true;
}

// Returns 0 or 1 depending if it's ok to add an else
bool maybeAddElse(uint cur, uint to)
{
	BlockStack *p;

	if (((to | cur) >> 16) || (to <= cur))
		return false;								/* Invalid jump */

	if (!num_block_stack)
		return false;								/* There are no previous blocks, so an else is not ok */

	p = &block_stack[num_block_stack - 1];
	if (cur != p->to)
		return false;								/* We have no prevoius if that is exiting right at the end of this goto */

	num_block_stack--;
	if (maybeAddIf(cur, to))
		return true;								/* We can add an else */
	num_block_stack++;
	return false;									/* An else is not OK here :( */
}

bool maybeAddElseIf(uint cur, uint elseto, uint to)
{
	uint k;
	BlockStack *p;

	if (((to | cur | elseto) >> 16) || (elseto < to) || (to <= cur))
		return false;								/* Invalid jump */

	if (!num_block_stack)
		return false;								/* There are no previous blocks, so an ifelse is not ok */

	p = &block_stack[num_block_stack - 1];

	if (p->isWhile)
		return false;

	if (scriptVersion == 8)
		k = to - 5;
	else
		k = to - 3;

	if (k >= size_of_code)
		return false;								/* Invalid jump */

	if (org_pos[k] != g_jump_opcode)
		return false;								/* Invalid jump */

	if (scriptVersion == 8)
		k = to + TO_LE_32(*(int32*)(org_pos + k + 1));
	else
		k = to + TO_LE_16(*(int16*)(org_pos + k + 1));

	if (k != elseto)
		return false;								/* Not an ifelse */

	p->from = cur;
	p->to = to;

	return true;
}

void writePendingElse()
{
	if (pendingElse) {
		char buf[32];
		sprintf(buf, alwaysShowOffs ? "} else /*%.4X*/ {" : "} else {", pendingElseTo);
		outputLine(buf, pendingElseOffs, pendingElseOpcode, pendingElseIndent - 1);
		pendingElse = false;
	}
}