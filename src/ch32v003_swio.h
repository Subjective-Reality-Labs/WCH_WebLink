// CH32V003 SWIO minimal reference implementation for bit banged IO
// on the ESP32-S2.
//
// Copyright 2023 Charles Lohr, May be licensed under the MIT/x11 or NewBSD
// licenses.  You choose.  (Can be included in commercial and copyleft work)
//
// This file is original work.
//
// Mostly tested, though, not perfect.  Expect to tweak some things.
// DoSongAndDanceToEnterPgmMode is almost completely untested.
// This is the weird song-and-dance that the WCH LinkE does when
// connecting to a CH32V003 part with unknown state.  This is probably
// incorrect, but isn't really needed unless things get really cursed.

// Updated and extended by monte-monte 2024

#ifndef _CH32V003_SWIO_H
#define _CH32V003_SWIO_H

#include "soc/gpio_struct.h"
// #include "soc/gpio_reg.h"
// #include "esp_attr.h"

#define MAX_IN_TIMEOUT 1000

#if 0
#define DisableISR()            do { XTOS_SET_INTLEVEL(XCHAL_EXCM_LEVEL); portbenchmarkINTERRUPT_DISABLE(); } while (0)
#define EnableISR()             do { portbenchmarkINTERRUPT_RESTORE(0); XTOS_SET_INTLEVEL(0); } while (0)
#else
#define DisableISR portDISABLE_INTERRUPTS
#define EnableISR portENABLE_INTERRUPTS
#endif

// This is a hacky thing, but if you are laaaaazzzyyyy and don't want to add a 10k
// resistor, youcan do this.  It glitches the line high very, very briefly.  
// Enable for when you don't have a 10k pull-upand are relying on the internal pull-up.
// WARNING: If you set this, you should set the drive current to 5mA.
// #define R_GLITCH_HIGH 

// You should interface to this file via these functions

struct SWIOState
{
	// Set these before calling any functions
	int t1coeff;
	int pinmask;

	// Zero the rest of the structure.
	uint32_t statetag;
	uint32_t lastwriteflags;
	uint32_t currentstateval;
	uint32_t flash_unlocked;
	uint32_t autoincrement;
};

#if  defined(CONFIG_IDF_TARGET_ESP32)
#define GPIO_IN GPIO.in
#define GPIO_SET GPIO.out_w1ts
#define GPIO_CLEAR GPIO.out_w1tc
#define GPIO_ENABLE_SET GPIO.enable_w1ts
#define GPIO_ENABLE_CLEAR GPIO.enable_w1tc
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
#define GPIO_IN GPIO.in.val
#define GPIO_SET GPIO.out_w1ts.val
#define GPIO_CLEAR GPIO.out_w1tc.val
#define GPIO_ENABLE_SET GPIO.enable_w1ts.val
#define GPIO_ENABLE_CLEAR GPIO.enable_w1tc.val
#endif

#define STTAG( x ) (*((uint32_t*)(x)))

#define IRAM IRAM_ATTR

static int DoSongAndDanceToEnterPgmMode( struct SWIOState * state );
static void MCFWriteReg32( struct SWIOState * state, uint8_t command, uint32_t value ) IRAM;
static int MCFReadReg32( struct SWIOState * state, uint8_t command, uint32_t * value ) IRAM;

// More advanced functions built on lower level PHY.
static int ReadWord( struct SWIOState * iss, uint32_t word, uint32_t * ret );
static int ReadByte( struct SWIOState * iss, uint32_t address_to_read, uint8_t * data );
static int WriteByte( struct SWIOState * iss, uint32_t address_to_write, uint8_t data );
static int WriteWord( struct SWIOState * state, uint32_t word, uint32_t val );
static int WaitForFlash( struct SWIOState * state );
static int WaitForDoneOp( struct SWIOState * state );
static int Write64Block( struct SWIOState * iss, uint32_t address_to_write, uint8_t * data );
static int ReadBinaryBlob( struct SWIOState * iss, uint32_t address_to_read_from,  uint32_t read_size, uint8_t * data );
static int WriteBinaryBlob( struct SWIOState * iss, uint32_t address_to_write, uint32_t blob_size, uint8_t * blob );
static int UnlockFlash( struct SWIOState * iss );
static int EraseFlash( struct SWIOState * iss, uint32_t address, uint32_t length, int type );
static void ResetInternalProgrammingState( struct SWIOState * iss );
static int PollTerminal( struct SWIOState * iss, uint8_t * buffer, int maxlen, uint32_t leavevalA, uint32_t leavevalB );
static int HaltMode( struct SWIOState * iss, int mode );

#define DMDATA0        0x04
#define DMDATA1        0x05
#define DMCONTROL      0x10
#define DMSTATUS       0x11
#define DMHARTINFO     0x12
#define DMABSTRACTCS   0x16
#define DMCOMMAND      0x17
#define DMABSTRACTAUTO 0x18
#define DMPROGBUF0     0x20
#define DMPROGBUF1     0x21
#define DMPROGBUF2     0x22
#define DMPROGBUF3     0x23
#define DMPROGBUF4     0x24
#define DMPROGBUF5     0x25
#define DMPROGBUF6     0x26
#define DMPROGBUF7     0x27

#define DMCPBR       0x7C
#define DMCFGR       0x7D
#define DMSHDWCFGR   0x7E

#define R32_FLASH_BASE 0x40022000
#define R32_FLASH_ACTLR 0x40022000
#define R32_FLASH_KEYR 0x40022004
#define R32_FLASH_OBKEYR 0x40022008
#define R32_FLASH_STATR 0x4002200C
#define R32_FLASH_CTLR 0x40022010
#define R32_FLASH_ADDR 0x40022014
#define R32_FLASH_OBR 0x4002201C
#define R32_FLASH_WPR 0x40022020
#define R32_FLASH_MODEKEYR 0x40022024
#define R32_FLASH_BOOT_MODEKEYR 0x40022028

/* FLASH Keys */
#define RDP_Key                    ((uint16_t)0x00A5)
#define FLASH_KEY1                 ((uint32_t)0x45670123)
#define FLASH_KEY2                 ((uint32_t)0xCDEF89AB)
#define CR_LOCK_Set                ((uint32_t)0x00000080)

#define FLASH_STATR_WRPRTERR       ((uint8_t)0x10) 
#define CR_PAGE_PG                 ((uint32_t)0x00010000)
#define CR_BUF_LOAD                ((uint32_t)0x00040000)
#define FLASH_CTLR_MER             ((uint16_t)0x0004)     /* Mass Erase */
#define CR_STRT_Set                ((uint32_t)0x00000040)
#define CR_PAGE_ER                 ((uint32_t)0x00020000)
#define CR_BUF_RST                 ((uint32_t)0x00080000)

static inline void Send1Bit( int t1coeff, int pinmask ) IRAM;
static inline void Send0Bit( int t1coeff, int pinmask ) IRAM;
static inline int ReadBit( struct SWIOState * state ) IRAM;

// dedic_gpio_bundle_handle_t swio_bundle;
// uint32_t swio_bit = 0;

static inline void PrecDelay( int delay )
{
#ifdef __XTENSA__
asm volatile( 
"1:	addi %[delay], %[delay], -1\n"
"	bbci %[delay], 31, 1b\n" : [delay]"+r"(delay)  );
#endif
#ifdef __riscv	
	asm volatile(
"1:	addi %[delay], %[delay], -1\n"
"bne %[delay], x0, 1b\n" :[delay]"+r"(delay)  );
#endif
}

// TODO: Add continuation (bypass) functions.
// TODO: Consider adding parity bit (though it seems rather useless)

// All three bit functions assume bus state will be in
// 	GPIO.out_w1ts.val = pinmask;
//	GPIO.enable_w1ts.val = pinmask;
// when they are called.
static inline void Send1Bit( int t1coeff, int pinmask )
{
	// Low for a nominal period of time.
	// High for a nominal period of time.

	GPIO_CLEAR = pinmask;
	// RV_WRITE_CSR(CSR_GPIO_OUT_USER, 0);
	PrecDelay( t1coeff );
	// RV_WRITE_CSR(CSR_GPIO_OUT_USER, 1);
	GPIO_SET = pinmask;
	PrecDelay( t1coeff );
}

static inline void Send0Bit( int t1coeff, int pinmask )
{
	// Low for a LONG period of time.
	// High for a nominal period of time.
	int longwait = t1coeff*4;
	// RV_WRITE_CSR(CSR_GPIO_OUT_USER, 0);
	GPIO_CLEAR = pinmask;
	PrecDelay( longwait );
	// RV_WRITE_CSR(CSR_GPIO_OUT_USER, 1);
	GPIO_SET = pinmask;
	PrecDelay( t1coeff );
}

// returns 0 if 0
// returns 1 if 1
// returns 2 if timeout.
static inline int ReadBit( struct SWIOState * state )
{
	int t1coeff = state->t1coeff;
	int pinmask = state->pinmask;

	// Drive low, very briefly.  Let drift high.
	// See if CH32V003 is holding low.

	int timeout = 0;
	int ret = 0;
	int medwait = t1coeff * 2;
	GPIO_CLEAR = pinmask;
	// RV_WRITE_CSR(CSR_GPIO_OUT_USER, 0);
	PrecDelay( t1coeff );
	GPIO_ENABLE_CLEAR = pinmask;
	// RV_WRITE_CSR(CSR_GPIO_OEN_USER, 0);
	GPIO_SET = pinmask;
	// RV_WRITE_CSR(CSR_GPIO_OUT_USER, 1);

#ifdef R_GLITCH_HIGH
	int halfwait = t1coeff / 2;
	PrecDelay( halfwait );
	GPIO_ENABLE_SET = pinmask;
	GPIO_ENABLE_CLEAR = pinmask;
	PrecDelay( halfwait );
#else
	PrecDelay( medwait );
#endif
	ret = GPIO_IN;
	// ret = RV_READ_CSR(CSR_GPIO_IN_USER);

#ifdef R_GLITCH_HIGH
	if( !(ret & pinmask) )
	{
		// Wait if still low.
		PrecDelay( medwait );
		GPIO_ENABLE_SET = pinmask;
		GPIO_ENABLE_CLEAR = pinmask;
	}
#endif
	for( timeout = 0; timeout < MAX_IN_TIMEOUT; timeout++ )
	{
		if( GPIO_IN & pinmask )
		// if( RV_READ_CSR(CSR_GPIO_IN_USER) )
		{
			GPIO_ENABLE_SET = pinmask;
			// RV_WRITE_CSR(CSR_GPIO_OEN_USER, 1);
			int fastwait = t1coeff / 2;
			PrecDelay( fastwait );
			return !!(ret & pinmask);
			// return !!(ret);
		}
	}
	
	// Force high anyway so, though hazarded, we can still move along.
	GPIO_ENABLE_SET = pinmask;
	// RV_WRITE_CSR(CSR_GPIO_OEN_USER, 1);
	return 2;
}

static void MCFWriteReg32( struct SWIOState * state, uint8_t command, uint32_t value )
{
	int t1coeff = state->t1coeff;
	int pinmask = state->pinmask;

 	GPIO_SET = pinmask;
	GPIO_ENABLE_SET = pinmask;

	DisableISR();
	Send1Bit( t1coeff, pinmask );
	uint32_t mask;
	for( mask = 1<<6; mask; mask >>= 1 )
	{
		if( command & mask )
			Send1Bit(t1coeff, pinmask);
		else
			Send0Bit(t1coeff, pinmask);
	}
	Send1Bit( t1coeff, pinmask );
	for( mask = 1<<31; mask; mask >>= 1 )
	{
		if( value & mask )
			Send1Bit(t1coeff, pinmask);
		else
			Send0Bit(t1coeff, pinmask);
	}
	EnableISR();
	esp_rom_delay_us(8); // Sometimes 2 is too short.
}

// returns 0 if no error, otherwise error.
static int MCFReadReg32( struct SWIOState * state, uint8_t command, uint32_t * value )
{
	int t1coeff = state->t1coeff;
	int pinmask = state->pinmask;

 	GPIO_SET = pinmask;
	GPIO_ENABLE_SET = pinmask;

	DisableISR();
	Send1Bit( t1coeff, pinmask );
	int i;
	uint32_t mask;
	for( mask = 1<<6; mask; mask >>= 1 )
	{
		if( command & mask )
			Send1Bit(t1coeff, pinmask);
		else
			Send0Bit(t1coeff, pinmask);
	}
	Send0Bit( t1coeff, pinmask );
	uint32_t rval = 0;
	for( i = 0; i < 32; i++ )
	{
		rval <<= 1;
		int r = ReadBit( state );
		if( r == 1 )
			rval |= 1;
		if( r == 2 )
		{
			EnableISR();
			return -1;
		}
	}
	*value = rval;
	EnableISR();
	esp_rom_delay_us(8); // Sometimes 2 is too short.
	return 0;
}

static inline void ExecuteTimePairs( struct SWIOState * state, const uint16_t * pairs, int numpairs, int iterations )
{
	int t1coeff = state->t1coeff;
	int pinmask = state->pinmask;
	int j, k;
	for( k = 0; k < iterations; k++ )
	{
		const uint16_t * tp = pairs;
		for( j = 0; j < numpairs; j++ )
		{
			int t1v = t1coeff * (*(tp++))-1;
			GPIO_CLEAR = pinmask;
			PrecDelay( t1v );
			GPIO_SET = pinmask;
			t1v = t1coeff * (*(tp++))-1;
			PrecDelay( t1v );
		}
	}
}

// Returns 0 if chips is present
// Returns 1 if chip is not present
// Returns 2 if there was a bus fault.
static int DoSongAndDanceToEnterPgmMode( struct SWIOState * state )
{
	int pinmask = state->pinmask;
	// XXX MOSTLY UNTESTED!!!!

	static const uint16_t timepairs1[] ={
		32, 12, //  8.2us / 3.1us
		36, 12, //  9.2us / 3.1us
		392, 366 // 102.3us / 95.3us
	};

	// Repeat for 199x
	static const uint16_t timepairs2[] ={
		15, 12, //  4.1us / 3.1us
		32, 12, //  8.2us / 3.1us
		36, 12, //  9.3us / 3.1us
		392, 366 // 102.3us / 95.3us
	};

	// Repeat for 10x
	static const uint16_t timepairs3[] ={
		15, 807, //  4.1us / 210us
		24, 8,   //  6.3us / 2us
		32, 8,   //  8.4us / 2us
		24, 10,  //  6.2us / 2.4us
		20, 8,   //  5.2us / 2.1us
		239, 8,  //  62.3us / 2.1us
		32, 20,  //  8.4us / 5.3us
		8, 32,   //  2.2us / 8.4us
		24, 8,   //  6.3us / 2.1us
		32, 8,   //  8.4us / 2.1us
		26, 7,   //  6.9us / 1.7us
		20, 8,   //  5.2us / 2.1us
		239, 8,  //  62.3us / 2.1us
		32, 20,  //  8.4us / 5.3us
		8, 22,   //  2us / 5.3us
		24, 8,   //  6.3us 2.1us
		24, 8,   //  6.3us 2.1us
		31, 6,   //  8us / 1.6us
		25, 307, // 6.6us / 80us
	};

	DisableISR();

	ExecuteTimePairs( state, timepairs1, 3, 1 );
	ExecuteTimePairs( state, timepairs2, 4, 199 );
	ExecuteTimePairs( state, timepairs3, 19, 10 );

	// THIS IS WRONG!!!! THIS IS NOT A PRESENT BIT. 
	int present = ReadBit( state ); // Actually here t1coeff, for this should be *= 8!
	GPIO_ENABLE_SET = pinmask;
	GPIO_CLEAR = pinmask;
	esp_rom_delay_us( 2000 );
	GPIO_SET = pinmask;
	EnableISR();
	esp_rom_delay_us( 1 );

	return present;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Higher level functions

static int WaitForFlash( struct SWIOState * iss )
{
	struct SWIOState * dev = iss;
	uint32_t rw, timeout = 0;
	do
	{
		rw = 0;
		ReadWord( dev, 0x4002200C, &rw ); // FLASH_STATR => 0x4002200C
	} while( (rw & 1) && timeout++ < 500);  // BSY flag.

	WriteWord( dev, 0x4002200C, 0 );

	if( rw & FLASH_STATR_WRPRTERR )
		return -44;


	if( rw & 1 )
		return -5;

	return 0;
}

static int WaitForDoneOp( struct SWIOState * iss )
{
	int r;
	uint32_t rrv;
	int ret = 0;
	struct SWIOState * dev = iss;
	do
	{
		r = MCFReadReg32( dev, DMABSTRACTCS, &rrv );
		if( r ) return r;
	}
	while( rrv & (1<<12) );
	if( (rrv >> 8 ) & 7 )
	{
		MCFWriteReg32( dev, DMABSTRACTCS, 0x00000700 );
		ret = -33;
	}
	return ret;
}

static void StaticUpdatePROGBUFRegs( struct SWIOState * dev )
{
	MCFWriteReg32( dev, DMDATA0, 0xe00000f4 );   // DATA0's location in memory.
	MCFWriteReg32( dev, DMCOMMAND, 0x0023100a ); // Copy data to x10
	MCFWriteReg32( dev, DMDATA0, 0xe00000f8 );   // DATA1's location in memory.
	MCFWriteReg32( dev, DMCOMMAND, 0x0023100b ); // Copy data to x11
	MCFWriteReg32( dev, DMDATA0, 0x40022010 ); //FLASH->CTLR
	MCFWriteReg32( dev, DMCOMMAND, 0x0023100c ); // Copy data to x12
	MCFWriteReg32( dev, DMDATA0, CR_PAGE_PG|CR_BUF_LOAD);
	MCFWriteReg32( dev, DMCOMMAND, 0x0023100d ); // Copy data to x13
}

static void ResetInternalProgrammingState( struct SWIOState * iss )
{
	iss->statetag = 0;
	iss->lastwriteflags = 0;
	iss->currentstateval = 0;
	iss->flash_unlocked = 0;
	iss->autoincrement = 0;
}

static int ReadWord( struct SWIOState * iss, uint32_t address_to_read, uint32_t * data )
{
	struct SWIOState * dev = iss;

	int autoincrement = 1;
	if( address_to_read == 0x40022010 || address_to_read == 0x4002200C )  // Don't autoincrement when checking flash flag. 
		autoincrement = 0;

	if( iss->statetag != STTAG( "RDSQ" ) || address_to_read != iss->currentstateval || autoincrement != iss->autoincrement )
	{
		if( iss->statetag != STTAG( "RDSQ" ) || autoincrement != iss->autoincrement )
		{
			MCFWriteReg32( dev, DMABSTRACTAUTO, 0 ); // Disable Autoexec.

			// c.lw x8,0(x11) // Pull the address from DATA1
			// c.lw x9,0(x8)  // Read the data at that location.
			MCFWriteReg32( dev, DMPROGBUF0, 0x40044180 );
			if( autoincrement )
			{
				// c.addi x8, 4
				// c.sw x9, 0(x10) // Write back to DATA0

				MCFWriteReg32( dev, DMPROGBUF1, 0xc1040411 );
			}
			else
			{
				// c.nop
				// c.sw x9, 0(x10) // Write back to DATA0

				MCFWriteReg32( dev, DMPROGBUF1, 0xc1040001 );
			}
			// c.sw x8, 0(x11) // Write addy to DATA1
			// c.ebreak
			MCFWriteReg32( dev, DMPROGBUF2, 0x9002c180 );

			if( iss->statetag != STTAG( "WRSQ" ) )
			{
				StaticUpdatePROGBUFRegs( dev );
			}
			MCFWriteReg32( dev, DMABSTRACTAUTO, 1 ); // Enable Autoexec (not autoincrement)
			iss->autoincrement = autoincrement;
		}

		MCFWriteReg32( dev, DMDATA1, address_to_read );
		MCFWriteReg32( dev, DMCOMMAND, 0x00241000 ); // Only execute.

		iss->statetag = STTAG( "RDSQ" );
		iss->currentstateval = address_to_read;

		WaitForDoneOp( dev );
	}

	if( iss->autoincrement )
		iss->currentstateval += 4;

	return MCFReadReg32( dev, DMDATA0, data );
}

static int ReadByte( struct SWIOState * iss, uint32_t address_to_read, uint8_t * data )
{
	int ret = 0;
	struct SWIOState * dev = iss;
	
	iss->statetag = STTAG( "XXXX" );

	MCFWriteReg32( dev, DMABSTRACTAUTO, 0x00000000 ); // Disable Autoexec.

	// Different address, so we don't need to re-write all the program regs.
	// lb x8,0(x9)  // Write to the address.
	MCFWriteReg32( dev, DMPROGBUF0, 0x00048403 ); // lb x8, 0(x9)
	MCFWriteReg32( dev, DMPROGBUF1, 0x00100073 ); // c.ebreak

	MCFWriteReg32( dev, DMDATA0, address_to_read );
	MCFWriteReg32( dev, DMCOMMAND, 0x00231009 ); // Copy data to x9
	MCFWriteReg32( dev, DMCOMMAND, 0x00241000 ); // Only execute.
	MCFWriteReg32( dev, DMCOMMAND, 0x00221008 ); // Read x8 into DATA0.

	ret |= WaitForDoneOp( dev );
	// if( ret ) fprintf( stderr, "Fault on DefaultReadByte\n" );
	iss->currentstateval = -1;

	uint32_t rr;
	ret |= MCFReadReg32( dev, DMDATA0, &rr );
	*data = rr;
	return ret;
}

static int WriteByte( struct SWIOState * iss, uint32_t address_to_write, uint8_t data )
{
	struct SWIOState * dev = iss;

	int ret = 0;
	iss->statetag = STTAG( "XXXX" );

	MCFWriteReg32( dev, DMABSTRACTAUTO, 0x00000000 ); // Disable Autoexec.

	// Different address, so we don't need to re-write all the program regs.
	// sh x8,0(x9)  // Write to the address.
	MCFWriteReg32( dev, DMPROGBUF0, 0x00848023 ); // sb x8, 0(x9)
	MCFWriteReg32( dev, DMPROGBUF1, 0x00100073 ); // c.ebreak

	MCFWriteReg32( dev, DMDATA0, address_to_write );
	MCFWriteReg32( dev, DMCOMMAND, 0x00231009 ); // Copy data to x9
	MCFWriteReg32( dev, DMDATA0, data );
	MCFWriteReg32( dev, DMCOMMAND, 0x00271008 ); // Copy data to x8, and execute program.

	ret |= WaitForDoneOp( dev );
	// if( ret ) fprintf( stderr, "Fault on DefaultWriteByte\n" );
	iss->currentstateval = -1;
	return ret;
}

static int WriteWord( struct SWIOState * iss, uint32_t address_to_write, uint32_t data )
{
	struct SWIOState * dev = iss;

	int ret = 0;

	int is_flash = 0;
	if( ( address_to_write & 0xff000000 ) == 0x08000000 || ( address_to_write & 0x1FFFF800 ) == 0x1FFFF000 )
	{
		// Is flash.
		is_flash = 1;
	}

	if( iss->statetag != STTAG( "WRSQ" ) || is_flash != iss->lastwriteflags )
	{
		int did_disable_req = 0;
		if( iss->statetag != STTAG( "WRSQ" ) )
		{
			MCFWriteReg32( dev, DMABSTRACTAUTO, 0x00000000 ); // Disable Autoexec.
			did_disable_req = 1;
			// Different address, so we don't need to re-write all the program regs.
			// c.lw x9,0(x11) // Get the address to write to. 
			// c.sw x8,0(x9)  // Write to the address.
			MCFWriteReg32( dev, DMPROGBUF0, 0xc0804184 );
			// c.addi x9, 4
			// c.sw x9,0(x11)
			MCFWriteReg32( dev, DMPROGBUF1, 0xc1840491 );

			if( iss->statetag != STTAG( "RDSQ" ) )
			{
				StaticUpdatePROGBUFRegs( dev );
			}
		}

		if( iss->lastwriteflags != is_flash || iss->statetag != STTAG( "WRSQ" ) )
		{
			// If we are doing flash, we have to ack, otherwise we don't want to ack.
			if( is_flash )
			{
				// After writing to memory, also hit up page load flag.
				// c.sw x13,0(x12) // Acknowledge the page write.
				// c.ebreak
				MCFWriteReg32( dev, DMPROGBUF2, 0x9002c214 );
			}
			else
			{
				MCFWriteReg32( dev, DMPROGBUF2, 0x00019002 ); // c.ebreak
			}
		}

		MCFWriteReg32( dev, DMDATA1, address_to_write );
		MCFWriteReg32( dev, DMDATA0, data );

		if( did_disable_req )
		{
			MCFWriteReg32( dev, DMCOMMAND, 0x00271008 ); // Copy data to x8, and execute program.
			MCFWriteReg32( dev, DMABSTRACTAUTO, 1 ); // Enable Autoexec.
		}
		iss->lastwriteflags = is_flash;


		iss->statetag = STTAG( "WRSQ" );
		iss->currentstateval = address_to_write;

		if( is_flash )
			ret |= WaitForDoneOp( dev );
	}
	else
	{
		if( address_to_write != iss->currentstateval )
		{
			MCFWriteReg32( dev, DMABSTRACTAUTO, 0 ); // Disable Autoexec.
			MCFWriteReg32( dev, DMDATA1, address_to_write );
			MCFWriteReg32( dev, DMABSTRACTAUTO, 1 ); // Enable Autoexec.
		}
		MCFWriteReg32( dev, DMDATA0, data );
		if( is_flash )
		{
			// XXX TODO: This likely can be a very short delay.
			// XXX POSSIBLE OPTIMIZATION REINVESTIGATE.
			ret |= WaitForDoneOp( dev );
		}
		else
		{
			ret |= WaitForDoneOp( dev );
		}
	}


	iss->currentstateval += 4;

	return 0;
}

static int UnlockFlash( struct SWIOState * iss )
{
	struct SWIOState * dev = iss;

	uint32_t rw;
	ReadWord( dev, 0x40022010, &rw );  // FLASH->CTLR = 0x40022010
	if( rw & 0x8080 ) 
	{

		WriteWord( dev, 0x40022004, 0x45670123 ); // FLASH->KEYR = 0x40022004
		WriteWord( dev, 0x40022004, 0xCDEF89AB );
		WriteWord( dev, 0x40022008, 0x45670123 ); // OBKEYR = 0x40022008
		WriteWord( dev, 0x40022008, 0xCDEF89AB );
		WriteWord( dev, 0x40022024, 0x45670123 ); // MODEKEYR = 0x40022024
		WriteWord( dev, 0x40022024, 0xCDEF89AB );

		ReadWord( dev, 0x40022010, &rw ); // FLASH->CTLR = 0x40022010
		if( rw & 0x8080 ) 
		{
			return -9;
		}
	}
	iss->flash_unlocked = 1;
	return 0;
}

static int EraseFlash( struct SWIOState * iss, uint32_t address, uint32_t length, int type )
{
	struct SWIOState * dev = iss;

	uint32_t rw;

	if( !iss->flash_unlocked )
	{
		if( ( rw = UnlockFlash( iss ) ) )
			return rw;
	}

	if( type == 1 )
	{
		// Whole-chip flash
		iss->statetag = STTAG( "XXXX" );
		// printf( "Whole-chip erase\n" );
		WriteWord( dev, 0x40022010, 0 ); //  FLASH->CTLR = 0x40022010
		WriteWord( dev, 0x40022010, FLASH_CTLR_MER  );
		WriteWord( dev, 0x40022010, CR_STRT_Set|FLASH_CTLR_MER );
		if( WaitForFlash( dev ) ) return -13;
		WriteWord( dev, 0x40022010, 0 ); //  FLASH->CTLR = 0x40022010
	}
	else
	{
		// 16.4.7, Step 3: Check the BSY bit of the FLASH_STATR register to confirm that there are no other programming operations in progress.
		// skip (we make sure at the end)

		int chunk_to_erase = address;

		while( chunk_to_erase < address + length )
		{
			if( WaitForFlash( dev ) ) return -14;

			// Step 4:  set PAGE_ER of FLASH_CTLR(0x40022010)
			WriteWord( dev, 0x40022010, CR_PAGE_ER ); // Actually FTER //  FLASH->CTLR = 0x40022010

			// Step 5: Write the first address of the fast erase page to the FLASH_ADDR register.
			WriteWord( dev, 0x40022014, chunk_to_erase  ); // FLASH->ADDR = 0x40022014

			// Step 6: Set the STAT bit of FLASH_CTLR register to '1' to initiate a fast page erase (64 bytes) action.
			WriteWord( dev, 0x40022010, CR_STRT_Set|CR_PAGE_ER );  // FLASH->CTLR = 0x40022010
			if( WaitForFlash( dev ) ) return -15;

			WriteWord( dev, 0x40022010, 0 ); //  FLASH->CTLR = 0x40022010 (Disable any pending ops)
			chunk_to_erase+=64;
		}
	}
	return 0;
}

static int Write64Block( struct SWIOState * iss, uint32_t address_to_write, uint8_t * blob )
{
	struct SWIOState * dev = iss;

	int blob_size = 64;
	uint32_t wp = address_to_write;
	uint32_t ew = wp + blob_size;
	int group = -1;
	int is_flash = 0;
	int rw = 0;

	if( (address_to_write & 0xff000000) == 0x08000000 || (address_to_write & 0xff000000) == 0x00000000  || (address_to_write & 0x1FFFF800) == 0x1FFFF000  ) 
	{
		// Need to unlock flash.
		// Flash reg base = 0x40022000,
		// FLASH_MODEKEYR => 0x40022024
		// FLASH_KEYR => 0x40022004

		if( !iss->flash_unlocked )
		{
			if( ( rw = UnlockFlash( dev ) ) )
				return rw;
		}

		is_flash = 1;
		rw = EraseFlash( dev, address_to_write, blob_size, 0 );
		if( rw ) return rw;
		// 16.4.6 Main memory fast programming, Step 5
		//if( WaitForFlash( dev ) ) return -11;
		//WriteWord( dev, 0x40022010, FLASH_CTLR_BUF_RST );
		//if( WaitForFlash( dev ) ) return -11;

	}

	/* General Note:
		Most flash operations take about 3ms to complete :(
	*/

	while( wp < ew )
	{
		if( is_flash )
		{
			group = (wp & 0xffffffc0);
			WriteWord( dev, 0x40022010, CR_PAGE_PG ); // THIS IS REQUIRED, (intptr_t)&FLASH->CTLR = 0x40022010   (PG Performs quick page programming operations.)
			WriteWord( dev, 0x40022010, CR_BUF_RST | CR_PAGE_PG );  // (intptr_t)&FLASH->CTLR = 0x40022010

			int j;
			for( j = 0; j < 16; j++ )
			{
				int index = (wp-address_to_write);
				uint32_t data = 0xffffffff;
				if( index + 3 < blob_size )
					data = ((uint32_t*)blob)[index/4];
				else if( (int32_t)(blob_size - index) > 0 )
				{
					memcpy( &data, &blob[index], blob_size - index );
				}
				WriteWord( dev, wp, data );
				//if( (rw = WaitForFlash( dev ) ) ) return rw;
				wp += 4;
			}
			WriteWord( dev, 0x40022014, group );
			WriteWord( dev, 0x40022010, CR_PAGE_PG|CR_STRT_Set );  // R32_FLASH_CTLR
			if( (rw = WaitForFlash( dev ) ) ) return rw;
		}
		else
		{
			int index = (wp-address_to_write);
			uint32_t data = 0xffffffff;
			if( index + 3 < blob_size )
				data = ((uint32_t*)blob)[index/4];
			else if( (int32_t)(blob_size - index) > 0 )
				memcpy( &data, &blob[index], blob_size - index );
			WriteWord( dev, wp, data );
			wp += 4;
		}
	}

	return 0;
}

int ReadBinaryBlob( struct SWIOState * iss, uint32_t address_to_read_from, uint32_t read_size, uint8_t * blob )
{
	struct SWIOState * dev = iss;

	uint32_t rpos = address_to_read_from;
	uint32_t rend = address_to_read_from + read_size;

	while( rpos < rend )
	{
		int r;
		int remain = rend - rpos;

		if( ( rpos & 3 ) == 0 && remain >= 4 )
		{
			uint32_t rw;
			r = ReadWord( dev, rpos, &rw );
			//printf( "RW: %d %08x %08x\n", r, rpos, rw );
			if( r ) return r;
			int rem = remain;
			if( rem > 4 ) rem = 4;
			memcpy( blob, &rw, rem);
			blob += 4;
			rpos += 4;
		}
		else
		{
			for ( ; remain > 0; remain--) {
				uint8_t rw;
				r = ReadByte( dev, rpos, &rw );
				if( r ) return r;
				memcpy( blob, &rw, 1 );
				blob += 1;
				rpos += 1;
			}
			// if( ( rpos & 1 ) )
			// {
			// 	uint8_t rw;
			// 	r = ReadByte( dev, rpos, &rw );
			// 	if( r ) return r;
			// 	memcpy( blob, &rw, 1 );
			// 	blob += 1;
			// 	rpos += 1;
			// 	remain -= 1;
			// }
			// if( ( rpos & 2 ) && remain >= 2 )
			// {
			// 	uint16_t rw;
			// 	r = ReadHalfWord( dev, rpos, &rw );
			// 	if( r ) return r;
			// 	memcpy( blob, &rw, 2 );
			// 	blob += 2;
			// 	rpos += 2;
			// 	remain -= 2;
			// }
			// if( remain >= 1 )
			// {
			// 	uint8_t rw;
			// 	r = ReadByte( dev, rpos, &rw );
			// 	if( r ) return r;
			// 	memcpy( blob, &rw, 1 );
			// 	blob += 1;
			// 	rpos += 1;
			// 	remain -= 1;
			// }
		}
	}
	int r = WaitForDoneOp( dev );
	// if( r ) fprintf( stderr, "Fault on DefaultReadBinaryBlob\n" );
	return r;
}

static int WriteBinaryBlob( struct SWIOState * iss, uint32_t address_to_write, uint32_t blob_size, uint8_t * blob )
{
	struct SWIOState * dev = iss;

	uint32_t rw;
	int is_flash = 0;

	if( blob_size == 0 ) return 0;

	if( (address_to_write & 0xff000000) == 0x08000000 || (address_to_write & 0xff000000) == 0x00000000 || (address_to_write & 0x1FFFF800) == 0x1FFFF000 ) 
		is_flash = 1;

	// We can't write into flash when mapped to 0x00000000
	if( is_flash )
		address_to_write |= 0x08000000;

	if( is_flash && ( address_to_write & 0x3f ) == 0 && ( blob_size & 0x3f ) == 0 )
	{
		int i;
		for( i = 0; i < blob_size; i+= 64 )
		{
			int r = Write64Block( dev, address_to_write + i, blob + i );
			if( r )
			{
				// fprintf( stderr, "Error writing block at memory %08x / Error: %d\n", address_to_write, r );
				return r;
			}
		}
		return 0;
	}

	if( is_flash && !iss->flash_unlocked )
	{
		if( ( rw = UnlockFlash( iss ) ) )
			return rw;
	}


	uint8_t tempblock[64];
	uint8_t tempblock2[64];
	// uint8_t *tempblock = (uint8_t*)malloc(64);
	int sblock =  address_to_write >> 6;
	int eblock = ( address_to_write + blob_size + 0x3f) >> 6;
	int b;
	int rsofar = 0;

	for( b = sblock; b < eblock; b++ )
	{
		int offset_in_block = address_to_write - (b * 64);
		if( offset_in_block < 0 ) offset_in_block = 0;
		int end_o_plus_one_in_block = ( address_to_write + blob_size ) - (b*64);
		if( end_o_plus_one_in_block > 64 ) end_o_plus_one_in_block = 64;
		int	base = b * 64;

		if( offset_in_block == 0 && end_o_plus_one_in_block == 64 )
		{
			int r;
			for(int i=0; i<20; i++) {
				r = Write64Block( dev, base, blob + rsofar );
				ReadBinaryBlob( dev, base, 64, tempblock );
				if (!memcmp(blob+rsofar, tempblock, 64)) break;
				if (i == 9) 
				{
					Serial.println(rsofar);
					return -99;
				}
			}
			rsofar += 64;
			if( r )
			{
				// fprintf( stderr, "Error writing block at memory %08x (error = %d)\n", base, r );
				return r;
			}
		}
		else
		{
			//Ok, we have to do something wacky.
			if( is_flash )
			{
				ReadBinaryBlob( dev, base, 64, tempblock );

				// Permute tempblock
				int tocopy = end_o_plus_one_in_block - offset_in_block;
				memcpy( tempblock + offset_in_block, blob + rsofar, tocopy );

				// int r = Write64Block( dev, base, tempblock );
				int r;
				for(int i=0; i<20; i++) {
					r = Write64Block( dev, base, tempblock );
					ReadBinaryBlob( dev, base, 64, tempblock2 );
					if (!memcmp(tempblock, tempblock2, 64)) break;
					if (i == 9) return -99;
				}
				if( r ) return r;
				if( WaitForFlash( dev ) ) goto timedout;
			}
			else
			{
				// Accessing RAM.  Be careful to only do the needed operations.
				int j;
				for( j = 0; j < 16; j++ )
				{
					uint32_t taddy = j*4;
					if( offset_in_block <= taddy && end_o_plus_one_in_block >= taddy + 4 )
					{
						WriteWord( dev, taddy + base, *(uint32_t*)(blob + rsofar) );
						rsofar += 4;
					}
					// else if( ( offset_in_block & 1 ) || ( end_o_plus_one_in_block & 1 ) )
					else
					{
						// Bytes only.
						int j;
						for( j = 0; j < 4; j++ )
						{
							if( taddy >= offset_in_block && taddy < end_o_plus_one_in_block )
							{
								WriteByte( dev, taddy + base, *(uint32_t*)(blob + rsofar) );
								rsofar ++;
							}
							taddy++;
						}
					}
					// else
					// {
					// 	// Half-words
					// 	int j;
					// 	for( j = 0; j < 4; j+=2 )
					// 	{
					// 		if( taddy >= offset_in_block && taddy < end_o_plus_one_in_block )
					// 		{
					// 			WriteHalfWord( dev, taddy + base, *(uint32_t*)(blob + rsofar) );
					// 			rsofar +=2;
					// 		}
					// 		taddy+=2;
					// 	}
					// }
				}
			}
		}
	}

	WaitForDoneOp( dev );
	// FlushLLCommands( dev );

	// if(MCF.DelayUS) MCF.DelayUS( dev, 100 ); // Why do we need this? (We seem to need this on the WCH programmers?)
	return 0;
timedout:
	// fprintf( stderr, "Timed out\n" );
	return -5;
}

// Polls up to 7 bytes of printf, and can leave a 7-bit flag for the CH32V003.
static int PollTerminal( struct SWIOState * iss, uint8_t * buffer, int maxlen, uint32_t leavevalA, uint32_t leavevalB )
{
	struct SWIOState * dev = iss;

	int r;
	uint32_t rr;
	if( iss->statetag != STTAG( "TERM" ) )
	{
		MCFWriteReg32( dev, DMABSTRACTAUTO, 0x00000000 ); // Disable Autoexec.
		iss->statetag = STTAG( "TERM" );
	}
	r = MCFReadReg32( dev, DMDATA0, &rr );
	if( r < 0 ) return r;

	if( maxlen < 8 ) return -9;

	// DMDATA1:
	//  bit  7 = host-acknowledge.
	if( rr & 0x80 )
	{
		int num_printf_chars = (rr & 0xf)-4;

		if( num_printf_chars > 0 && num_printf_chars <= 7)
		{
			if( num_printf_chars > 3 )
			{
				uint32_t r2;
				r = MCFReadReg32( dev, DMDATA1, &r2 );
				memcpy( buffer+3, &r2, num_printf_chars - 3 );
			}
			int firstrem = num_printf_chars;
			if( firstrem > 3 ) firstrem = 3;
			memcpy( buffer, ((uint8_t*)&rr)+1, firstrem );
			buffer[num_printf_chars] = 0;
		}
		if( leavevalA )
		{
			MCFWriteReg32( dev, DMDATA1, leavevalB );
		}
		MCFWriteReg32( dev, DMDATA0, leavevalA ); // Write that we acknowledge the data.
		if( num_printf_chars == 0 ) return -1;      // was acked?
		if( num_printf_chars < 0 ) num_printf_chars = 0;
		return num_printf_chars;
	}
	else
	{
		return 0;
	}
}

static int HaltMode( struct SWIOState * iss, int mode )
{
	struct SWIOState * dev = iss;

	switch ( mode )
	{
	case 5: // Don't reboot.
	case 0:	// Reboot into Halt
		MCFWriteReg32( dev, DMSHDWCFGR, 0x5aa50000 | (1<<10) ); // Shadow Config Reg
		MCFWriteReg32( dev, DMCFGR, 0x5aa50000 | (1<<10) ); // CFGR (1<<10 == Allow output from slave)
		MCFWriteReg32( dev, DMCFGR, 0x5aa50000 | (1<<10) ); // Bug in silicon?  If coming out of cold boot, and we don't do our little "song and dance" this has to be called.

		MCFWriteReg32( dev, DMCONTROL, 0x80000001 ); // Make the debug module work properly.
		if( mode == 0 ) MCFWriteReg32( dev, DMCONTROL, 0x80000003 ); // Reboot.
		MCFWriteReg32( dev, DMCONTROL, 0x80000001 ); // Re-initiate a halt request.

//		MCF.WriteReg32( dev, DMCONTROL, 0x00000001 ); // Clear Halt Request.  This is recommended, but not doing it seems more stable.
		// Sometimes, even if the processor is halted but the MSB is clear, it will spuriously start?
		WaitForDoneOp( dev );
		break;
	case 1:	// Reboot
		MCFWriteReg32( dev, DMCONTROL, 0x80000001 ); // Make the debug module work properly.
		MCFWriteReg32( dev, DMCONTROL, 0x80000001 ); // Initiate a halt request.
		MCFWriteReg32( dev, DMCONTROL, 0x80000003 ); // Reboot.
		MCFWriteReg32( dev, DMCONTROL, 0x40000001 ); // resumereq
		WaitForDoneOp( dev );
		break;
	case 2:	// Resume
		MCFWriteReg32( dev, DMSHDWCFGR, 0x5aa50000 | (1<<10) ); // Shadow Config Reg
		MCFWriteReg32( dev, DMCFGR, 0x5aa50000 | (1<<10) ); // CFGR (1<<10 == Allow output from slave)
		MCFWriteReg32( dev, DMCFGR, 0x5aa50000 | (1<<10) ); // Bug in silicon?  If coming out of cold boot, and we don't do our little "song and dance" this has to be called.

		MCFWriteReg32( dev, DMCONTROL, 0x40000001 ); // resumereq
		WaitForDoneOp( dev );
		break;
	case 3:	// Rebot into bootloader`
		MCFWriteReg32( dev, DMCONTROL, 0x80000001 ); // Make the debug module work properly.
		MCFWriteReg32( dev, DMCONTROL, 0x80000001 ); // Initiate a halt request.

		// WriteWord( dev, (intptr_t)&FLASH->KEYR, FLASH_KEY1 );
		// WriteWord( dev, (intptr_t)&FLASH->KEYR, FLASH_KEY2 );
		// WriteWord( dev, (intptr_t)&FLASH->BOOT_MODEKEYR, FLASH_KEY1 );
		// WriteWord( dev, (intptr_t)&FLASH->BOOT_MODEKEYR, FLASH_KEY2 );
		// WriteWord( dev, (intptr_t)&FLASH->STATR, 1<<14 );
		// WriteWord( dev, (intptr_t)&FLASH->CTLR, CR_LOCK_Set );
		WriteWord( dev, R32_FLASH_KEYR, FLASH_KEY1 );
		WriteWord( dev, R32_FLASH_KEYR, FLASH_KEY2 );
		WriteWord( dev, R32_FLASH_BOOT_MODEKEYR, FLASH_KEY1 );
		WriteWord( dev, R32_FLASH_BOOT_MODEKEYR, FLASH_KEY2 );
		WriteWord( dev, R32_FLASH_STATR, 1<<14 );
		WriteWord( dev, R32_FLASH_CTLR, CR_LOCK_Set );

		MCFWriteReg32( dev, DMCONTROL, 0x80000003 ); // Reboot.
		MCFWriteReg32( dev, DMCONTROL, 0x40000001 ); // resumereq
		WaitForDoneOp( dev );
		break;
	// default:
		// fprintf( stderr, "Error: Unknown halt mode %d\n", mode );
	}

	return 0;
}

#endif // _CH32V003_SWIO_H

