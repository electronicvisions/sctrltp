#ifndef SCTRL_FILE_H
#define SCTRL_FILE_H
/*
	Stefan Philipp
	Electronic Vision(s)
	University of Heidelberg, Germany
	
	sctrl_file.h
	
	Implements the slow control commant set, 
	but writes all commands to testbench
	file to be read by vhdl/verilog testbench
	or by user for inline debugging.
	function naming: SCTRL_File_*
	
	HISTORY
	
	12/2005, Stefan Philipp, creation
*/

int SCTRL_File_Open(int interactive);
int SCTRL_File_Close();

/* time for read, write and compare
 * returns last time value
 * set file descriptor: ("stdio" FILE *)
 */
void SCTRL_File_SetFile(FILE* stream);

/* set file descriptor: ("stdio" FILE *) */
void SCTRL_File_SetRecFile(FILE* stream);

/* returns last time value */
void SCTRL_File_SetWait(unsigned int time);

/* single commands returns, how many data are successfully transferred, -1 = error
 * in interactive mode if only the command was written to file, 0 is returned
 */
int SCTRL_File_Read(int sctrl, int nathan, int module, unsigned int addr, unsigned int* value);

/* single commands returns, how many data are successfully transferred, -1 = error */
int SCTRL_File_Write(int sctrl, int nathan, int module, unsigned int addr, unsigned int value);

/* returns 1 if equal and 0 if not */
int SCTRL_File_Compare(int sctrl, int nathan, int module, unsigned int addr, unsigned int value, unsigned int mask);

/* returns 1 if ok and 0 if timeout */
int SCTRL_File_Poll(int sctrl, int nathan, int module, unsigned int addr, unsigned int value, unsigned int mask, int timeout);

/* block commands return number of successfully moved dwords */
int SCTRL_File_ReadBlock(int sctrl, int nathan, int module, unsigned int addr, unsigned int* buf, int num);
int SCTRL_File_WriteBlock(int sctrl, int nathan, int module, unsigned int addr, unsigned int* buf, int num);

#endif
