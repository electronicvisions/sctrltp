/*
	Stefan Philipp
	Electronic Vision(s)
	University of Heidelberg, Germany
	
	sctrl_file.c
	
	
	Implements the slow control commant set, 
	but writes all commands to testbench
	file to be read by vhdl/verilog testbench
	or by user for inline debugging.
	Read commands read from file.
	
	Should be possible to use this for interactive
	modelsimming (to be tested)
	
	function naming: SCTRL_File_*

	Files used:
	
	In current implementation, no default file
	exists and files are opened/closed by user.
	If desired, implement file open/close in
	SetFile and call this by open/close (i.e. with NULL)

	NOTE
	
	As all these functions are called from one of the sctrl.c implementations,
	no additional error checking of "sctrl" etc is made here 
	
	HISTORY
	
	12/2005, Stefan Philipp, creation
*/

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#include "sctrltp/sctrl_file.h"

FILE* 			outfile;
FILE* 			infile;
int 			mode_interactive;
unsigned int	waittime;

#ifndef ull 
#define ull unsigned long long
#endif

/* -------------------------------------------------------------- *
 * SCTRL file functions                                           *
 * -------------------------------------------------------------- */

int SCTRL_File_Open(int interactive)
{
	outfile	= NULL; /* init undefined */
	infile  = NULL; /* init undefined */
	mode_interactive = interactive; /* do not wait */
	waittime=0; /* init nowait */
	
	if(mode_interactive) 
		fprintf(stderr, "SCTRL_File_Open(): opening interactive mode\n");
	
	return(-1);
}


int SCTRL_File_Close()
{
	/* in current implementation, no default file
	 * exists and files are opened/closed by user
	 */
	
	/* in interactive mode, leave the simulator command file open
	 * in non-interactive mode, end simulation
	 */
	if ((outfile) && !mode_interactive)
		fprintf(outfile, "e\n");
	return(-1);
}


/* time for read, write and compare
 * returns last time value
 * set file descriptor: ("stdio" FILE *)
 */
void SCTRL_File_SetFile(FILE* stream)
{
	outfile=stream;
	fprintf(outfile, "#\n# Time N M    ByteAddr  Value     Mask\n#\n");
	fflush(outfile);
}


void SCTRL_File_SetRecFile(FILE* stream)
{
	infile=stream;
}


/* returns last time value */
void SCTRL_File_SetWait(unsigned int wait)
{
	waittime=wait;
}


/*
 * try to decode std_logic states from hex nibble
 * assume, that single stdl values as 'W' can be set for a "nibble"
 * of 4 bits if all of these belong to this state
 * 
 * Commented this out - it is in case only usefull if a whole bitvector is written out
 * to handle the mask correctly (e.g. xxx0 --> "X", but bit 0 valid)
 */
/*
 *unsigned int SCTRL_File_Stlv_to_int(char * stdlv, unsigned int mask)
 *{
 *    int i, nib; unsigned int value; char dummy;
 *
 *    value=0;
 *    for(i=0; i<8; i++)
 *    {
 *        nib=0;
 *        if ((mask>>(7-i))&15)
 *        {	
 *            if (sscanf(&stdlv[i], "%1x", &nib)) {}
 *            else
 *            if (stdlv[i]=='L') nib=0;
 *            else
 *            if (stdlv[i]=='H') nib=1;
 *            else		
 *            if (sscanf(&stdlv[i], "*%1[XUZW-]", &dummy))
 *                fprintf(stderr, "SCTRL_File_ReadResult(): ERROR: enmasked invalid values in simulator return value\n");
 *            else
 *                fprintf(stderr, "SCTRL_File_ReadResult(): ERROR: invalid simulator return value\n");
 *        }
 *        value=value*16+nib;
 *    }
 *    return(value);
 *}
 */


int SCTRL_File_ReadResult(char c, unsigned int mask, unsigned int* value)
{
	int ret, wait; int echo; char cmd; /* char result[9]; */
	(void) mask; /* TODO: unused parameter */
	
	wait=time(0);
	echo=0;
	
	if (infile==NULL) printf ("SCTRL_File_ReadResult(): infile is not valid\n");
	
	while((ret=fscanf(infile, " %c %08x ", &cmd, value))==EOF)
	{
		/* input file is EOF, wait in interactive mode
		 * until new data appears */
		
		if (!mode_interactive) return(0); /* just return in non-interactive mode on eofs */
		
		usleep(200000); /* wait 200 msecs */
		if (time(0)-wait>2)
		{
			if(!echo)
			{ 	
				fprintf(stderr, "SCTRL_File_Read(): waiting for simulator...");
				echo=1;
			}
			else fprintf(stderr, ".");
			fflush(stdout);
			wait=time(0);
		}
	}
	if (echo) fprintf(stderr, "\n");
	/* fprintf(stderr, "c=%c, res_len=%d, ret=%d\n", cmd, strlen(result), ret); */

	if(ret!=2) 
	{
		fprintf(stderr, "SCTRL_File_ReadResult(): parse ERROR reading from input file\n");
		return(0);
	}
	if(c!=cmd)
	{
		fprintf(stderr, "SCTRL_File_ReadResult(): Consistency error, expected '%c' result, but found '%c'\n", c, cmd);
		return(0);
	}

/*	*value=SCTRL_File_Stlv_to_int(result, mask); */

	return(1);
}


/* single commands return 1 on success and 0 on error */
int SCTRL_File_Read(int sctrl, int nathan, int module, unsigned int addr, unsigned int* value)
{
	(void) sctrl; /* TODO: unused parameter */

	if (outfile)
	{
		if (!fprintf(outfile, "r %04x %01x %01x %09llx\n", waittime, nathan, module, ((ull)addr)*4))
		{
			fprintf(stderr, "SCTRL_File_Read(): ERROR writing to output file\n");
			return(0);
		}
		fflush(outfile);
	}
	
	if(infile) 
		return(SCTRL_File_ReadResult('r', -1, value));
	
	/* command only written, no data read */
	return(0);
}


/* single commands return 1 on success and 0 on error */
int SCTRL_File_Write(int sctrl, int nathan, int module, unsigned int addr, unsigned int value)
{
	(void) sctrl; /* TODO: unused parameter */

	if (outfile)
	{	
		if (!fprintf(outfile, "w %04x %01x %01x %09llx %08x\n", waittime, nathan, module, ((ull)addr)*4, value))
		{
			fprintf(stderr, "SCTRL_File_Write(): ERROR writing to output file\n");
			return(0);
		}
		fflush(outfile);
	}		
	return(1);
}


/* returns 1 if equal and 0 if not */
int SCTRL_File_Compare(int sctrl, int nathan, int module, unsigned int addr, unsigned int value, unsigned int mask)
{
	unsigned int data;

	(void) sctrl; /* TODO: unused parameter */
	
	if (outfile)
	{
		if (!fprintf(outfile, "c %04x %01x %01x %09llx %08x %08x\n", waittime, nathan, module, ((ull)addr)*4, value, mask))
		{
			fprintf(stderr, "SCTRL_File_Compare(): ERROR writing to output file\n");
			return(0);
		}
		fflush(outfile);
	}		

	if(infile)
		return (SCTRL_File_ReadResult('c', -1, &data) && data);
		
	/* command only written */
	return(0);
}


/* returns 1 if ok and 0 if timeout */
int SCTRL_File_Poll(int sctrl, int nathan, int module, unsigned int addr, unsigned int value, unsigned int mask, int timeout)
{
	unsigned int data;

	(void) sctrl; /* TODO: unused parameter */

	if (outfile)
	{
		if (!fprintf(outfile, "p %04x %01x %01x %09llx %08x %08x\n", timeout, nathan, module, ((ull)addr)*4, value, mask))
		{
			fprintf(stderr, "SCTRL_File_Poll(): ERROR writing to output file\n");
			return(0);
		}
		fflush(outfile);
	}	

	if(infile) 
		return (SCTRL_File_ReadResult('p', -1, &data) && data);
		
	/* command only written */
	return(0);
}


/* block commands return number of successfully moved dwords */
int SCTRL_File_ReadBlock(int sctrl, int nathan, int module, unsigned int addr, unsigned int* buf, int num)
{
	int n=0;
	if (outfile)
		for(n=0;n<num;n++)
			if (!SCTRL_File_Read(sctrl, nathan, module, addr+n, &buf[n]))
				break;
	return(n);
}			


/* block commands return number of successfully moved dwords */
int SCTRL_File_WriteBlock(int sctrl, int nathan, int module, unsigned int addr, unsigned int* buf, int num)
{
	int n=0;
	if (outfile)
		for(n=0;n<num;n++)
			if (!SCTRL_File_Write(sctrl, nathan, module, addr+n, buf[n]))
				break;
	return(n);
}			
