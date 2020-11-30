
/* $Id: memory.c,v 1.2 2000/05/25 22:28:56 jholder Exp $   
 * --------------------------------------------------------------------
 * see doc/License.txt for License Information   
 * --------------------------------------------------------------------
 * 
 * File name: $Id: memory.c,v 1.2 2000/05/25 22:28:56 jholder Exp $  
 *   
 * Description:    
 *    
 * Modification history:      
 * $Log: memory.c,v $
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
 * memory.c
 *
 * Code and data caching routines
 *
 */

#include "ztypes.h"

/* A cache entry */

typedef struct cache_entry
{
   struct cache_entry *flink;
   unsigned int page_key;
   zbyte_t data[PAGE_SIZE];
}
cache_entry_t;

/* Cache chain anchor */

static cache_entry_t *cache = NULL;

/* Pseudo translation buffer, one entry each for code and data pages */

static unsigned int current_code_page = 0;
static cache_entry_t *current_code_cachep = NULL;
static unsigned int current_data_page = 0;
static cache_entry_t *current_data_cachep = NULL;

static unsigned int calc_data_pages( void );
static cache_entry_t *update_cache( unsigned int );

/*
 * load_cache
 *
 * Initialise the cache and any other dynamic memory objects. The memory
 * required can be split into two areas. Firstly, three buffers are required for
 * input, output and status line. Secondly, two data areas are required for
 * writeable data and read only data. The writeable data is the first chunk of
 * the file and is put into non-paged cache. The read only data is the remainder
 * of the file which can be paged into the cache as required. Writeable data has
 * to be memory resident because it cannot be written out to a backing store.
 *
 */

void load_cache( void )
{
   unsigned long file_size;
   unsigned int i, file_pages, data_pages;
   cache_entry_t *cachep;

   /* Allocate output and status line buffers */

   line = ( char * ) malloc( screen_cols + 1 );
   if ( line == NULL )
   {
      fatal( "load_cache(): Insufficient memory to play game" );
   }
   status_line = ( char * ) malloc( screen_cols + 1 );
   if ( status_line == NULL )
   {
      fatal( "load_cache(): Insufficient memory to play game" );
   }

   /* Must have at least one cache page for memory calculation */
   cachep = ( cache_entry_t * ) malloc( sizeof ( cache_entry_t ) );

   if ( cachep == NULL )
   {
      fatal( "load_cache(): Insufficient memory to play game" );
   }
   cachep->flink = cache;
   cachep->page_key = 0;
   cache = cachep;

   /* Calculate dynamic cache pages required */

   if ( h_config & CONFIG_MAX_DATA )
   {
      data_pages = calc_data_pages(  );
   }
   else
   {
      data_pages = ( h_data_size + PAGE_MASK ) >> PAGE_SHIFT;
   }
   data_size = data_pages * PAGE_SIZE;
   file_size = ( unsigned long ) h_file_size *story_scaler;

   file_pages = ( unsigned int ) ( ( file_size + PAGE_MASK ) >> PAGE_SHIFT );

   /* Allocate static data area and initialise it */

   datap = ( zbyte_t * ) malloc( data_size );
   if ( datap == NULL )
   {
      fatal( "load_cache(): Insufficient memory to play game" );
   }
   for ( i = 0; i < data_pages; i++ )
   {
      read_page( i, &datap[i * PAGE_SIZE] );
   }

   /* Allocate memory for undo */

   undo_datap = ( zbyte_t * ) malloc( data_size );

   /* Allocate cache pages and initialise them */

   for ( i = data_pages; cachep != NULL && i < file_pages; i++ )
   {
      cachep = ( cache_entry_t * ) malloc( sizeof ( cache_entry_t ) );

      if ( cachep != NULL )
      {
         read_page( i, cachep->data );
         cachep->flink = cache;
         cachep->page_key = i<<PAGE_SHIFT;
         cache = cachep;
      }
   }

}                               /* load_cache */

/*
 * unload_cache
 *
 * Deallocate cache and other memory objects.
 *
 */

void unload_cache( void )
{
   cache_entry_t *cachep, *nextp;

   /* Make sure all output has been flushed */

   z_new_line(  );

   /* Free output buffer, status line and data memory */

   free( line );
   free( status_line );
   free( datap );
   free( undo_datap );

   /* Free cache memory */

   for ( cachep = cache; cachep->flink != NULL; cachep = nextp )
   {
      nextp = cachep->flink;
      free( cachep );
   }

}                               /* unload_cache */

/*
 * read_code_word
 *
 * Read a word from the instruction stream.
 *
 */

zword_t read_code_word( void )
{
#ifdef __mc68000__
#ifdef __GNUC__
	register zword_t d0 asm("d0");
	asm volatile (
	"\tjbsr %1\n"
	"\tlsl.w #8,%0\n"
	"\tmove.w %0,-(sp)\n"
	"\tjbsr %1\n"
	"\tand.w #255,%0\n"
	"\tor.w (sp)+,%0\n"
	: "=d" (d0) : "m" (read_code_byte) : "cc");
	return d0;
#else
	union {zword_t w; zbyte_t hilo[2];} t;
	t.hilo[0] = read_code_byte();
	t.hilo[1] = read_code_byte();
	return t.w;
#endif
#else
   zword_t w;

   w = ( zword_t ) read_code_byte(  ) << 8;
   w |= ( zword_t ) read_code_byte(  );

   return ( w );
#endif
}                               /* read_code_word */

/*
 * read_code_byte
 *
 * Read a byte from the instruction stream.
 *
 */

zbyte_t read_code_byte( void )
{
	/* Calculate page and offset values && update PC */
#if defined(__GNUC__) && defined(__mc68000__)
	register unsigned page_key asm("d0") = pc;
	asm volatile("andi%.w %1,%0" : "+d" (page_key): "J" (~PAGE_MASK));
#else
	unsigned int page_key = pc & ~PAGE_MASK;

#endif
	
		
	/* Load page into translation buffer */
	if ( page_key - current_code_page )
	{
#if defined(__GNUC__) && defined(__mc68000__) && !defined(PC_REG)
		asm volatile("move.l %0,-(sp)" : : "r" (page_key) : "sp");
#endif
		if ( !(current_code_cachep = update_cache( page_key )) )
		{
			fatal
					( "read_code_byte(): read from non-existant page!\n\t(Your dynamic memory usage _may_ be over 64k in size!)" );
		}
#if defined(__GNUC__) && defined(__mc68000__)
#ifndef PC_REG
		asm volatile("move.l (sp)+,%0" : "=r" (page_key) : : "sp");
#else
		page_key = pc;asm volatile("andi%.w %1,%0" : "+d" (page_key): "J" (~PAGE_MASK));
#endif
#else
		page_key = pc & ~PAGE_MASK;
#endif
		current_code_page = page_key;
	}
	
	/* Return byte & update PC */
#if defined(__GNUC__) && defined(__mc68000__) && 0
	asm volatile(
	"	sub%.l	%1,%0\n"
	"	addq%.l	#1,%1\n"
	"	neg%.l	%0\n"
	: "+d" (page_key)
	: "m" (pc));
#elif defined(__GNUC__) && defined(__mc68000__)
#ifdef PC_REG
	asm volatile(
	"	eor%.w	%1,%0\n"
	"	addq%.l	#1,%1\n"
	: "+d" (page_key), "+d" (pc));
#else
	asm volatile(
	"	neg%.w	%0\n"
	"	add%.w	%2,%0\n"
	"	addq%.l	#1,%1\n"
	: "+d" (page_key)
	: "m" (pc), "m" (((zword_t*)&pc)[1]));
#endif
#elif defined(__GNUC__) && defined(__mc68000__) && 0
{ struct cache_entry *a0 = current_code_cachep;
	asm volatile(
	"   add%.l  %2,%0\n"
	"	addq%.l	#1,%2\n"
	"   sub%.l	%1,%0\n"
	: "=&a" (a0)
	: "d" (page_key), "m" (pc), "0" (a0));
return a0->data[0];}
#elif defined(__GNUC__) && defined(__mc68000__) && 0
	return ((struct cache_entry*)(((zbyte_t*)current_code_cachep) + pc++ - page_key))->data[0];
#else
	page_key ^= pc++;
#endif
	return current_code_cachep->data[(short)page_key];
}                               /* read_code_byte */

/*
 * read_data_word
 *
 * Read a word from the data area.
 *
 */

zword_t read_data_word( unsigned long *addr )
{
#ifdef __mc68000__
#ifdef __GNUC__
	register unsigned long *a0 asm("a0") = addr;
	register int ret asm("d0");
	asm volatile (
	"\tmove.l %2,-(sp)\n"
	"\tjbsr %1\n"
	"\tmove.l (sp),%2\n"
	"\tlsl.w #8,%0\n"
	"\tmove.l %0,(sp)\n"
	"\tjbsr %1\n"
	"\tand.w #255,d0\n"
	"\tor.l (sp)+,d0\n"
	: "=d" (ret) : "m" (read_data_byte), "a" (a0) : "cc");
	return ret;
#else
	union {zword_t w; zbyte_t hilo[2];} t;
	t.hilo[0] = read_data_byte(addr);
	t.hilo[1] = read_data_byte(addr);
	return t.w;
#endif
#else
   zword_t w;

   w = ( zword_t ) read_data_byte( addr ) << 8;
   w |= ( zword_t ) read_data_byte( addr );

   return ( w );
#endif
}                               /* read_data_word */

/*
 * read_data_byte
 *
 * Read a byte from the data area.
 *
 */

zbyte_t read_data_byte( unsigned long *addr )
{
	zbyte_t value = 0;
	unsigned int page_key = *addr;
	
	/* Check if byte is in non-paged cache */
	
	if ( page_key < ( unsigned long ) data_size )
	{
		/* fetch value & update address */
		value = datap[page_key];
	}
	else
	{
	/* Calculate page and offset values */
		page_key &= ~PAGE_MASK;
		/* Load page into translation buffer */
		if ( page_key != current_data_page )
		{
			if(!(current_data_cachep = update_cache( page_key ))) 
			{
				fatal( "read_data_byte(): Fetching data from invalid page!" );
			}
			current_data_page = page_key = *addr & ~PAGE_MASK;
		}
	
		/* fetch value & update address */
		value = current_data_cachep->data[page_key ^= (*addr)];
	}
	++*addr;
	
	return ( value );
}                               /* read_data_byte */

/*
 * calc_data_pages
 *
 * Compute the best size for the data area cache. Some games have the data size
 * header parameter set too low. This causes a write outside of data area on
 * some games. To alleviate this problem the data area size is set to the
 * maximum of the restart size, the data size and the end of the dictionary. An
 * attempt is made to put the dictionary in the data area to stop paging during
 * a dictionary lookup. Some games have the dictionary end very close to the
 * 64K limit which may cause problems for machines that allocate memory in
 * 64K chunks.
 *
 */

static unsigned int calc_data_pages( void )
{
   unsigned long offset, data_end, dictionary_end;
   int separator_count, word_size, word_count;
   unsigned int data_pages;

   /* Calculate end of data area, use restart size if data size is too low */

   if ( h_data_size > h_restart_size )
   {
      data_end = h_data_size;
   }
   else
   {
      data_end = h_restart_size;
   }

   /* Calculate end of dictionary table */

   offset = h_words_offset;
   separator_count = read_data_byte( &offset );
   offset += separator_count;
   word_size = read_data_byte( &offset );
   word_count = read_data_word( &offset );
   dictionary_end = offset + ( word_size * word_count );

   /* If data end is too low then use end of dictionary instead */

   if ( dictionary_end > data_end )
   {
      data_pages = ( unsigned int ) ( ( dictionary_end + PAGE_MASK ) >> PAGE_SHIFT );
   }
   else
   {
      data_pages = ( unsigned int ) ( ( data_end + PAGE_MASK ) >> PAGE_SHIFT );
   }

   return ( data_pages );

}                               /* calc_data_pages */

/*
 * update_cache
 *
 * Called on a code or data page cache miss to find the page in the cache or
 * read the page in from disk. The chain is kept as a simple LRU chain. If a
 * page cannot be found then the page on the end of the chain is reused. If the
 * page is found, or reused, then it is moved to the front of the chain.
 *
 */

static cache_entry_t *update_cache( unsigned int page_key )
{
   cache_entry_t *cachep, *lastp;

   /* Search the cache chain for the page */

   for ( lastp = cache, cachep = cache;
         cachep->page_key && cachep->page_key != page_key && cachep->flink != NULL;
         lastp = cachep, cachep = cachep->flink )
      ;

   /* If no page in chain then read it from disk */

   if ( cachep->page_key != page_key )
   {
      /* Reusing last cache page, so invalidate cache if page was in use */
      if ( cachep->flink == NULL && cachep->page_key )
      {
         if ( current_code_page == cachep->page_key )
         {
            current_code_page = 0;
         }
         if ( current_data_page == cachep->page_key )
         {
            current_data_page = 0;
         }
      }

      /* Load the new page number and the page contents from disk */

      cachep->page_key = page_key;
      read_page( page_key>>PAGE_SHIFT, cachep->data );
   }

   /* If page is not at front of cache chain then move it there */

   if ( lastp != cache )
   {
      lastp->flink = cachep->flink;
      cachep->flink = cache;
      cache = cachep;
   }

   return ( cachep );

}                               /* update_cache */
