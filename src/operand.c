
/* $Id: operand.c,v 1.2 2000/05/25 22:28:56 jholder Exp $   
 * --------------------------------------------------------------------
 * see doc/License.txt for License Information   
 * --------------------------------------------------------------------
 * 
 * File name: $Id: operand.c,v 1.2 2000/05/25 22:28:56 jholder Exp $  
 *   
 * Description:    
 *    
 * Modification history:      
 * $Log: operand.c,v $
 * Revision 1.2  2000/05/25 22:28:56  jholder
 * changes routine names to reflect zmachine opcode names per spec 1.0
 *
 * Revision 1.1.1.1  2000/05/10 14:21:34  jholder
 *
 * imported
 *
 *
 * --------------------------------------------------------------------
 */

/*
 * operand.c
 *
 * Operand manipulation routines
 *
 */

#include "ztypes.h"

/*
 * load_operand
 *
 * Load an operand, either: a variable, popped from the stack or a literal.
 *
 */

zword_t load_operand( int type )
{
#if 1
#if defined(__mc68000__) && defined(__GNUC__) && 0
	register int d0 asm("d0") = type;
	asm volatile (
	"	tst%.l 	%0\n"
	"	jbeq 	%2\n"
	"	subq%.l	#2,%0\n"
	"	jbeq	.l%=\n"
	"	jbsr	%1\n"
	"	and%.w	#255,d0\n"
	"	rts\n"
	".l%=:\n"
	"	jbsr	%1\n"
	"	and%.w  #255,d0\n"
	"	ext.l   d0\n"
	"	jbne	%3\n"
	"	move%.w	%5,d0\n"
	"	addq.w	#1,%5\n"
	"	add%.l	d0,d0\n"
	"	lea		%4,a0\n"
	"	move%.w	(a0,d0.l),%0\n"
	: "+d" (d0)
	: "m" (read_code_byte), "m" (read_code_word), "m" (load_variable), "m" (stack), "mr" (sp) : "a0", "d1", "a1", "cc", "memory");
	return d0;
#elif defined(__mc68000__) && defined(__GNUC__)
	register short d0 asm("d0") = type;
	if(!d0) return read_code_word( );
	d0 -= 2;
	if(!d0) {
		asm volatile (
		"	jbsr	%1\n"
		"	and%.w #255,d0\n"
		"	ext%.l	d0\n"
		"	jbne	%2\n"
		"	move%.w	%3,d0\n"
		"	addq.w	#1,%3\n"
		: "=d" (d0)
		: "m" (read_code_byte), "m" (load_variable), "mr" (sp));
		return stack[d0];
	}
	asm volatile("" : : "d" (d0));
	return read_code_byte(  );	
#else
	switch(type) {
		default: 
		return read_code_byte(  );	
		
		case 0: 
		return read_code_word( );
		
		case 2: 
		{zbyte_t t = read_code_byte(  );
		return t ? load_variable(t) : stack[sp++];}
	}
#endif
#else
   /*zword_t*/ zbyte_t operand;

   if ( type )
   {

      /* Type 1: byte literal, or type 2: operand specifier */

      operand = ( zword_t ) read_code_byte(  );
      if ( type == 2 )
      {

         /* If operand specifier non-zero then it's a variable, otherwise
          * it's the top of the stack */

         if ( operand )
         {
            return load_variable( operand );
         }
         else
         {
            return stack[sp++];
         }
      }
   }
   else
   {
      /* Type 0: word literal */
      return read_code_word(  );
   }
   return ( operand );
#endif
}                               /* load_operand */

/*
 * store_operand
 *
 * Store an operand, either as a variable pushed on the stack.
 *
 */

void store_operand( zword_t operand )
{
#if defined(__GNUC__) && defined(__mc68000__)
	register int d0 asm("d0"), d1 asm("d1");
	asm volatile(
	"	move%.l	%0,-(sp)\n"
	"	jbsr	%2\n"
	"	and%.w	#255,d0\n"
	"	move%.l	(sp)+,d1\n"
	"	ext%.l	d0\n"
	"	jbne	%3\n"
	: "=d" (d0), "=d" (d1)
	: "m" (read_code_byte), "m" (z_store)
	: "a0", "a1");
	asm volatile(
	"	subq%.w	#1,%2\n"
	"	move%.w	%2,%0\n"
	"	add%.l	%0,%0\n"
	"	move%.w	%1,(%3,%0.l)\n"
	: "+d" (d0)
	: "d" (d1), "m" (sp), "a" (stack));
#else
   zbyte_t specifier;

   /* Read operand specifier byte */

   specifier = read_code_byte(  );

   /* If operand specifier non-zero then it's a variable, otherwise it's the
    * pushed on the stack */

   if ( specifier )
      z_store( specifier, operand );
   else
      stack[--sp] = operand;
#endif
}                               /* store_operand */

/*
 * load_variable
 *
 * Load a variable, either: a stack local variable, a global variable or
 * the top of the stack.
 *
 */

zword_t load_variable( int number )
{
#if 1
	short d0 = number;
	if(0==d0) return stack[sp];
	if(0==(d0 & ~15)) return stack[fp - --d0];
	return get_word( (short)h_globals_offset + ( ( d0 - 16 ) * 2 ) );
#else
   if ( number )
   {
      if ( number < 16 )
      {
         /* number in range 1 - 15, it's a stack local variable */
         return stack[fp - ( number - 1 )];
      }
      else
      {
         /* number > 15, it's a global variable */
         return get_word( h_globals_offset + ( ( number - 16 ) * 2 ) );
      }
   }
   else
   {
      /* number = 0, get from top of stack */
      return stack[sp];
   }
#endif
}                               /* load_variable */

/*
 * z_store
 *
 * Store a variable, either: a stack local variable, a global variable or the
 * top of the stack.
 *
 */

void z_store( int number, zword_t variable )
{
#if 1
	short d0 = number;
	if(0==d0) stack[(short)sp] = variable; else
	if(d0<16) stack[(short)fp - --d0] = variable; else
	set_word( h_globals_offset + ( ( d0 - 16 ) * 2 ), variable);
#else
   if ( number )
   {
      if ( number < 16 )

         /* number in range 1 - 15, it's a stack local variable */

         stack[fp - ( number - 1 )] = variable;
      else
         /* number > 15, it's a global variable */

         set_word( h_globals_offset + ( ( number - 16 ) * 2 ), variable );
   }
   else
      /* number = 0, get from top of stack */

      stack[sp] = variable;
#endif
}                               /* z_store */

/*
 * z_piracy
 *
 * Supposed to jump if the game thinks that it is pirated.
 */
void z_piracy( int flag )
{
   conditional_jump( flag );
}

/*
 * conditional_jump
 *
 * Take a jump after an instruction based on the flag, either true or false. The
 * jump can be modified by the change logic flag. Normally jumps are taken
 * when the flag is true. When the change logic flag is set then the jump is
 * taken when flag is false. A PC relative jump can also be taken. This jump can
 * either be a positive or negative byte or word range jump. An additional
 * feature is the return option. If the jump offset is zero or one then that
 * literal value is passed to the return instruction, instead of a jump being
 * taken. Complicated or what!
 *
 */

void conditional_jump( int flag )
{
   zbyte_t specifier;
   zword_t offset;

   /* Read the specifier byte */

   specifier = read_code_byte(  );

   /* If the reverse logic flag is set then reverse the flag */

   if ( specifier & 0x80 )
      flag = ( flag ) ? 0 : 1;

   /* Jump offset is in bottom 6 bits */

   offset = ( zword_t ) specifier & 0x3f;

   /* If the byte range jump flag is not set then load another offset byte */

   if ( ( specifier & 0x40 ) == 0 )
   {

      /* Add extra offset byte to existing shifted offset */

      offset = ( offset << 8 ) + read_code_byte(  );

      /* If top bit of offset is set then propogate the sign bit */

      if ( offset & 0x2000 )
         offset |= 0xc000;
   }

   /* If the flag is false then do the jump */

   if ( flag == 0 )
   {

      /* If offset equals 0 or 1 return that value instead */

      if ( offset == 0 || offset == 1 )
      {
         z_ret( offset );
      }
      else
      {                         /* Add offset to PC */
         pc = ( unsigned long ) ( pc + ( ZINT16 ) offset - 2 );
      }
   }
}                               /* conditional_jump */
