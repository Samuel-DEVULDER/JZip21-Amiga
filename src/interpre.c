
/* $Id: interpre.c,v 1.3 2000/07/05 15:20:34 jholder Exp $   
 * --------------------------------------------------------------------
 * see doc/License.txt for License Information   
 * --------------------------------------------------------------------
 * 
 * File name: $Id: interpre.c,v 1.3 2000/07/05 15:20:34 jholder Exp $  
 *   
 * Description:    
 *    
 * Modification history:      
 * $Log: interpre.c,v $
 * Revision 1.3  2000/07/05 15:20:34  jholder
 * Updated code to remove warnings.
 *
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
// #define DEBUG_TERPRE
/*
 * interpre.c
 *
 * Main interpreter loop
 *
 */

#include "ztypes.h"

/*#define DEBUG_TERPRE*/

static zword_t halt = FALSE;

/*
 * interpret
 *
 * Interpret Z code
 *
 */

int interpret(  )
{
   static zword_t operand[8];

   interpreter_status = 1;

   /* Loop until HALT instruction executed */

   for ( interpreter_state = RUN; interpreter_state /*== RUN*/ && halt == FALSE; )
   {
		zbyte_t opcode, extended;
   		int count;

      /* Load opcode and set operand count */


      opcode = read_code_byte(  );
      if ( opcode == 0xbe && h_type > V4 )
      {
         opcode = read_code_byte(  );
         extended = TRUE;
      }
      else
         extended = FALSE;
      
      /* Multiple operand instructions */
#ifndef __GNUC__
#define __builtin_expect(x,y) (x)
#endif

      // if ( ( opcode < 0x80 || opcode > 0xc0 ) || extended==TRUE )
	  if (extended || //( opcode < 0x80 || opcode > 0xc0 ) ) 
	      __builtin_expect((opcode^0x80)>0x40,1))
      {

         /* Two operand class, load both operands */

         // if ( opcode < 0x80 && extended == FALSE )
         // {
            // operand[count++] = load_operand( ( opcode & 0x40 ) ? 2 : 1 );
            // operand[count++] = load_operand( ( opcode & 0x20 ) ? 2 : 1 );
            // opcode &= 0x1f;
         // }

         if ( __builtin_expect(!extended && opcode < 0x80,1) )
         {
#if defined(__GNUC__) && defined(__mc68000__) && 0

// movq					4
// and	opcode,d0			4
// jmp xx(pc,d0)			14 => 22 + 10 + 8 ~40

// 64 octets libres


// moveq 2
// jbsr	2
// moew	4
// moveq 2
// jbsr	2
// bra		2 => 14

            register zbyte_t d0 asm("d0");
            asm volatile (
            "       lsl%.b  #2,%0\n"		// 10
            "       jbcs    .l1x%=\n"		// 10 / 8
            "       jbmi    .l01x%=\n"		// 10 / 8
            "       moveq   #1,d0\n"		// 4 => 26
            "       jbsr    (%1)\n"
            "       move%.w d0,%2\n"			
            "       moveq   #1,d0\n"		// 4
            "       jbra    .lxx%=\n"		// 10 => 44
            ".l01x%=:\n"
            "       moveq   #1,d0\n"		// 4
            "       jbsr    (%1)\n"
            "       move%.w d0,%2\n"
            "       moveq   #2,d0\n"		// 4
            "       jbra    .lxx%=\n"		// 10 => 46
            ".l1x%=:\n"
            "       jbmi    .l11x%=\n"		// 10 / 8
            "       moveq   #2,d0\n"		// 4
            "       jbsr    (%1)\n"
            "       move%.w d0,%2\n"
            "       moveq   #1,d0\n"		// 4
            "       jbra    .lxx%=\n"		// 10 => 46
            ".l11x%=:\n"
            "       moveq   #2,d0\n"		// 4
            "       jbsr    (%1)\n"			// 4
            "       move%.w d0,%2\n"
            "       moveq   #2,d0\n"		// 4 ==> 42
            ".lxx%=:\n"
            "       jbsr    (%1)\n"
            "       move%.w d0,%3\n"
            : "=&d" (d0) : "a" (load_operand), "ma" (operand[0]), "ma" (operand[1]), "0" (opcode));
			opcode &= 0x1f;					// 8 ==> 52 / 50
#elif defined(__GNUC__) && defined(__mc68000__) && 0
			asm volatile (
			"	moveq	#0x40,d0\n"		// 4
			"	and%.w	%0,d0\n"		// 8
			"	seq		d0\n"			// 6
			"	addq%.b	#2,d0\n"		// 4 => 22
			"	jbsr	(%1)\n"
			"	move%.w	d0,%2\n"
			"	moveq	#0x20,d0\n"		// 4
			"	and%.w	%0,d0\n"		// 8
			"	seq		d0\n"			// 6
			"	addq%.b	#2,d0\n"		// 4 => 44
			"	jbsr	(%1)\n"
			"	move%.w	d0,%3\n"
			: : "d" (opcode), "a" (load_operand), "m" (operand[0]), "m" (operand[1])
			: "d0", "d1", "a0", "a1" );
			opcode &= 0x1f;				// 8 => 52 <<<<<
#elif defined(__GNUC__) && defined(__mc68000__) && 0
			// operand[0] = load_operand(0x40 & opcode? 2 : 1);
			// operand[1] = load_operand(0x20 & opcode? 2 : 1);
			asm volatile(
			"	add%.b	%0,%0\n"	// 4
			"	moveq	#0,d0\n"	// 4
			"	add%.b	%0,%0\n"	// 4
			"	addx%.w	d0,d0\n"	// 4
			"	addq%.w	#1,d0\n"	// 4 => 20
			"	jbsr	(%1)\n"
			"	move%.w	d0,%2\n"
			"	moveq	#0,d0\n"	// 4
			"	add%.b	%0,%0\n"	// 4
			"	addx%.w	d0,d0\n"	// 4
			"	addq%.w	#1,d0\n"	// 4 => 16 => 36
			"	jbsr	(%1)\n"
			"	move%.w	d0,%3\n"
			"	lsr%.b	#3,%0\n"	// 12 => 48 <<
			: "+d" (opcode) : "a" (load_operand), "m" (operand[0]), "m" (operand[1])
			: "d0", "d1", "a0", "a1");
#elif defined(__GNUC__) && defined(__mc68000__)
			// operand[0] = load_operand(0x40 & opcode? 2 : 1);
			// operand[1] = load_operand(0x20 & opcode? 2 : 1);
			asm volatile(
			"	moveq	#0x60,d0\n"
			"	and%.b	%0,d0\n"
			"	jmp	(.l00_%=,pc,d0.w)\n"
			".l00_%=:\n"
			"	moveq	#1,d0\n"
			"	jbsr	(%1)\n"
			"	move%.w	d0,%2\n"
			"	moveq	#1,d0\n"
			"	pea		(.lxx_%=,pc)\n"
			"	jbra	(%1)\n"
			"	.space	.l00_%=+32-.\n"
			".l01_%=:\n"
			"	moveq	#1,d0\n"
			"	jbsr	(%1)\n"
			"	move%.w	d0,%2\n"
			"	moveq	#2,d0\n"
			"	pea		(.lxx_%=,pc)\n"
			"	jbra	(%1)\n"
			"	.space	.l01_%=+32-.\n"
			".l10_%=:\n"
			"	moveq	#2,d0\n"
			"	jbsr	(%1)\n"
			"	move%.w	d0,%2\n"
			"	moveq	#1,d0\n"
			"	pea		(.lxx_%=,pc)\n"
			"	jbra	(%1)\n"
			"	.space	.l10_%=+32-.\n"
			".l11_%=:\n"
			"	moveq	#2,d0\n"
			"	jbsr	(%1)\n"
			"	move%.w	d0,%2\n"
			"	moveq	#2,d0\n"
			"	jbsr	(%1)\n"
			".lxx_%=:\n"
			"	move%.w	d0,%3\n"
			// "	and%.w	#0x1f,%0\n"
			: "+d" (opcode) : "a" (load_operand), "m" (operand[0]), "m" (operand[1])
			: "d0", "d1", "a0", "a1");
#else
			 switch(opcode&0x60) {
				 case 0x00:
				 operand[0] = load_operand(1);
				 operand[1] = load_operand(1);
				 break;
				 
				 case 0x20:
				 operand[0] = load_operand(1);
				 operand[1] = load_operand(2);
				 break;
				 
				 case 0x40:
				 operand[0] = load_operand(2);
				 operand[1] = load_operand(1);
				 break;
				 
				 default: 
				 operand[0] = load_operand(2);
				 operand[1] = load_operand(2);
				 break;
			 }
#endif
			 opcode &= 0x1f;
			 count = 2; 
			 goto not_extended;
         }
         else
         {
			zword_t specifier, *a2 = operand;
            /* Variable operand class, load operand specifier */

            opcode &= 0x3f;
            if ( opcode == 0x2c || opcode == 0x3a )
            {                   /* Extended CALL instruction */
			   specifier = ~read_code_word(  );
            }
            else
            {
               specifier = (~read_code_byte(  )) << 8;
            }
			while(specifier>=0x4000) {
				// 00 --> 11 --> >= 0xc000
				// 01 --> 10 --> >= 0x8000 
				// 10 --> 01 --> <  0x8000
				// 11 --> 00 --> <  0x4000
				if(specifier >= 0xc000) 
					*a2++ = load_operand(0);
				else if(specifier >= 0x8000)
					*a2++ = load_operand(1);
				else
					*a2++ = load_operand(2);
				specifier <<= 2;
			}
			count = a2 - operand;
         }

         if ( extended /*== TRUE*/ )
         {
#ifdef DEBUG_TERPRE
            fprintf( stderr, "PC = 0x%08lx   Op%s = 0x%02x   %d, %d, %d\n", pc, "(EX)", opcode,
                     operand[0], operand[1], operand[2] );
#endif
            switch ( /*( char )*/ opcode )
            {

                  /* Extended operand instructions */

               case 0x00:
                  z_save( count, operand[0], operand[1], operand[2] );
                  break;
               case 0x01:
                  z_restore( count, operand[0], operand[1], operand[2] );
                  break;
               case 0x02:
                  z_log_shift( operand[0], operand[1] );
                  break;
               case 0x03:
                  z_art_shift( operand[0], operand[1] );
                  break;
               case 0x04:
                  z_set_font( operand[0] );
                  break;
		// 05 ==> z_draw_picture
		// 06 ==> z_picture_data
		// 07 ==> z_erase_picture
		// 08 ==> z_set_margins

               case 0x09:
                  z_save_undo(  );
                  break;
               case 0x0a:
                  z_restore_undo(  );
                  break;
				  // 0b ==> z_print_unicode
				  // 0c ==> z_check_unicode

			   case 0x0c:
					z_check_unicode( operand[0] );
					break;

               default:
                  fatal( "interpret(): Illegal extended operand instruction" );
            }
         }
         else
         {
			 not_extended:
#ifdef DEBUG_TERPRE
            fprintf( stderr, "PC = 0x%08lx   Op%s = 0x%02x   %d, %d, %d\n", pc, "(2+)", opcode,
                     operand[0], operand[1], operand[2] );
#endif
            switch ( /*( char )*/ opcode )
            {

                  /* Two or multiple operand instructions */
               case 0x01:
                  z_je( count, operand );
                  break;
               case 0x02:
                  z_jl( operand[0], operand[1] );
                  break;
               case 0x03:
                  z_jg( operand[0], operand[1] );
                  break;
               case 0x04:
                  z_dec_chk( operand[0], operand[1] );
                  break;
               case 0x05:
                  z_inc_chk( operand[0], operand[1] );
                  break;
               case 0x06:
                  z_jin( operand[0], operand[1] );
                  break;
               case 0x07:
                  z_test( operand[0], operand[1] );
                  break;
               case 0x08:
                  z_or( operand[0], operand[1] );
                  break;
               case 0x09:
                  z_and( operand[0], operand[1] );
                  break;
               case 0x0a:
                  z_test_attr( operand[0], operand[1] );
                  break;
               case 0x0b:
                  z_set_attr( operand[0], operand[1] );
                  break;
               case 0x0c:
                  z_clear_attr( operand[0], operand[1] );
                  break;
               case 0x0d:
                  z_store( operand[0], operand[1] );
                  break;
               case 0x0e:
                  z_insert_obj( operand[0], operand[1] );
                  break;
               case 0x0f:
                  z_loadw( operand[0], operand[1] );
                  break;
               case 0x10:
                  z_loadb( operand[0], operand[1] );
                  break;
               case 0x11:
                  z_get_prop( operand[0], operand[1] );
                  break;
               case 0x12:
                  z_get_prop_addr( operand[0], operand[1] );
                  break;
               case 0x13:
                  z_get_next_prop( operand[0], operand[1] );
                  break;
               case 0x14:
                  z_add( operand[0], operand[1] );
                  break;
               case 0x15:
                  z_sub( operand[0], operand[1] );
                  break;
               case 0x16:
                  z_mul( operand[0], operand[1] );
                  break;
               case 0x17:
                  z_div( operand[0], operand[1] );
                  break;
               case 0x18:
                  z_mod( operand[0], operand[1] );
                  break;
               case 0x19:
                  z_call( count, operand, FUNCTION );
                  break;
               case 0x1a:
                  z_call( count, operand, PROCEDURE );
                  break;
               case 0x1b:
                  z_set_colour( operand[0], operand[1] );
                  break;
               case 0x1c:
                  z_throw( operand[0], operand[1] );
                  break;

                  /* Multiple operand instructions */

               case 0x20:
                  z_call( count, operand, FUNCTION );
                  break;
               case 0x21:
                  z_storew( operand[0], operand[1], operand[2] );
                  break;
               case 0x22:
                  z_storeb( operand[0], operand[1], operand[2] );
                  break;
               case 0x23:
                  z_put_prop( operand[0], operand[1], operand[2] );
                  break;
               case 0x24:
                  z_sread_aread( count, operand );
                  break;
               case 0x25:
                  z_print_char( operand[0] );
                  break;
               case 0x26:
                  z_print_num( operand[0] );
                  break;
               case 0x27:
                  z_random( operand[0] );
                  break;
               case 0x28:
                  z_push( operand[0] );
                  break;
               case 0x29:
                  z_pull( operand[0] );
                  break;
               case 0x2a:
                  z_split_window( operand[0] );
                  break;
               case 0x2b:
                  z_set_window( operand[0] );
                  break;
               case 0x2c:
                  z_call( count, operand, FUNCTION );
                  break;
               case 0x2d:
                  z_erase_window( operand[0] );
                  break;
               case 0x2e:
                  z_erase_line( operand[0] );
                  break;
               case 0x2f:
                  z_set_cursor( operand[0], operand[1] );
                  break;

			   case 0x30:
					z_get_cursor(operand[0] );
					break;

               case 0x31:
                  z_set_text_style( operand[0] );
                  break;
               case 0x32:
                  z_buffer_mode( operand[0] );
                  break;
               case 0x33:
                  z_output_stream( operand[0], operand[1] );
                  break;
               case 0x34:
                  z_input_stream( operand[0] );
                  break;
               case 0x35:
                  sound( count, operand );
                  break;
               case 0x36:
                  z_read_char( count, operand );
                  break;
               case 0x37:
                  z_scan_table( count, operand );
                  break;
               case 0x38:
                  z_not( operand[0] );
                  break;
               case 0x39:
                  z_call( count, operand, PROCEDURE );
                  break;
               case 0x3a:
                  z_call( count, operand, PROCEDURE );
                  break;
               case 0x3b:
                  z_tokenise( count, operand );
                  break;
               case 0x3c:
                  z_encode( operand[0], operand[1], operand[2], operand[3] );
                  break;
               case 0x3d:
                  z_copy_table( operand[0], operand[1], operand[2] );
                  break;
               case 0x3e:
                  z_print_table( count, operand );
                  break;
               case 0x3f:
                  z_check_arg_count( operand[0] );
                  break;

               default:
                  fatal( "interpret(): Illegal 2 or more operand instruction" );
            }
         }
      }
      else
      {

         /* Single operand class, load operand and execute instruction */

         if ( opcode < 0xb0 )
         {
            operand[0] = load_operand( ( opcode >> 4 ) & 0x03 );
#ifdef DEBUG_TERPRE
            fprintf( stderr, "PC = 0x%08lx   Op%s = 0x%02x   %d\n", pc, "(1 )", opcode,
                     operand[0] );
#endif
            switch ( ( char ) opcode & 0x0f )
            {
               case 0x00:
                  z_jz( operand[0] );
                  break;
               case 0x01:
                  z_get_sibling( operand[0] );
                  break;
               case 0x02:
                  z_get_child( operand[0] );
                  break;
               case 0x03:
                  z_get_parent( operand[0] );
                  break;
               case 0x04:
                  z_get_prop_len( operand[0] );
                  break;
               case 0x05:
                  z_inc( operand[0] );
                  break;
               case 0x06:
                  z_dec( operand[0] );
                  break;
               case 0x07:
                  z_print_addr( operand[0] );
                  break;
               case 0x08:
                  z_call( 1, operand, FUNCTION );
                  break;
               case 0x09:
                  z_remove_obj( operand[0] );
                  break;
               case 0x0a:
                  z_print_obj( operand[0] );
                  break;
               case 0x0b:
                  z_ret( operand[0] );
                  break;
               case 0x0c:
                  z_jump( operand[0] );
                  break;
               case 0x0d:
                  z_print_paddr( operand[0] );
                  break;
               case 0x0e:
                  z_load( operand[0] );
                  break;
               case 0x0f:
                  if ( h_type > V4 )
                     z_call( 1, operand, PROCEDURE );
                  else
                     z_not( operand[0] );
                  break;
            }
         }
         else
         {

            /* Zero operand class, execute instruction */
#ifdef DEBUG_TERPRE
            fprintf( stderr, "PC = 0x%08lx   Op%s = 0x%02x\n", pc, "(0 )", opcode );
#endif
            switch ( ( char ) opcode & 0x0f )
            {
               case 0x00:
                  z_ret( TRUE );
                  break;
               case 0x01:
                  z_ret( FALSE );
                  break;
               case 0x02:
                  z_print(  );
                  break;
               case 0x03:
                  z_print_ret(  );
                  break;
               case 0x04:
                  /* z_nop */
                  break;
               case 0x05:
                  z_save( 0, 0, 0, 0 );
                  break;
               case 0x06:
                  z_restore( 0, 0, 0, 0 );
                  break;
               case 0x07:
                  z_restart(  );
                  break;
               case 0x08:
                  z_ret( stack[sp++] );
                  break;
               case 0x09:
                  z_catch(  );
                  break;
               case 0x0a:
                  halt = TRUE;  /* z_quit */
                  break;
               case 0x0b:
                  z_new_line(  );
                  break;
               case 0x0c:
                  z_show_status(  );
                  break;
               case 0x0d:
                  z_verify(  );
                  break;

               case 0x0f:
                  z_piracy( TRUE );
                  break;

               default:
                  fatal( "interpret(): Illegal zero operand instruction" );
            }
         }
      }
   }

   return ( interpreter_status );

}                               /* interpret */
