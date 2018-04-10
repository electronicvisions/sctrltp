#include <stdio.h>

int fwriteswap(FILE* file, unsigned int l)
{
  unsigned int swap;
  swap=((l & 0xff)<<24) | ((l & 0xff00)<<8) | ((l >>8) & 0xff00) | ((l>>24)&0xff);
  return (fwrite(&swap, 4, 1, file));
}


// parse bitfile and output bitstream info
//
// returns size of bitstream inside
// sets offset of bitstream 

int ParseBitfile(char* filename, long int* ofs, int verbose)
{
  FILE* file;
  int count;
  unsigned short len;
  unsigned char typ;
  char buf[255];

  if (!filename) return(-1);
  if (!(file=fopen(filename, "r")))
  {
    fprintf(stderr, "sctrl.ParseBitfile(): error opening file '%s'\n", filename); 
    perror (""); 
    return(-1);
  }

  fread(&len, 2, 1, file);
  len=((len >> 8) & 255)+((len & 255)<<8);
  fseek(file, len, SEEK_CUR);
  fread(&len, 2, 1, file);  
  
  typ=0;
  while((typ!=0x65) || feof(file))
  {
    fread(&typ, 1, 1, file);
    fread(&len, 2, 1, file);
    len=((len >> 8) & 255)+((len & 255)<<8);
    if (typ!=0x65)
    {
      fread(buf, 1, len, file);
      if (verbose) fprintf(stderr, "%s ", buf); 
    }
  }
  count=len;
  fread(&len, 2, 1, file);
  count=(count<<16)|((len & 255) << 8)|((len >>8)&255);

  *ofs=ftell(file);
  if (verbose)
    fprintf(stderr, "\nbitstream of 0x%x bytes starting offset 0x%0lx\n", count, *ofs);
 
  fclose(file);
  return(count);
}

// CreateSerialBitstream()
//
// creates a serial bitstream to programm
// several xilinx devices in a daisy chain 
//
// return 0 on success
// -1 on error
int CreateSerialBitstream(char* outfilename, char** bitfilenames, int num)
{
	unsigned int size; int value;
	FILE* outfile;
	FILE* bitfile;
	
	int streamsize [16];
	long int streamofs [16];
	
	int i,j;
	
	
	// assemble size and offset of the single bitfiles
	// needed for data forward information inside the bitstream
	
	for (i=0; i<num ; i++)
	{
		streamsize[i] = ParseBitfile(bitfilenames[i], &streamofs[i], 0 /* verbose */);
		if (streamsize[i] == -1) return(-1);
		else streamsize[i] /= 4;
	}
	
	// bitstream saved in temp output file
	outfile = fopen(outfilename, "w");
	if (!outfile)
	{
		perror("CreateSerialBitstream(): error opening temp file for writing ...\n");
		return(-1);
	}
	
	for (i=0; i<num ;i++)
	{
		// write bitstream to config one device
		bitfile = fopen(bitfilenames[i], "r");
		fseek(bitfile, streamofs[i], SEEK_SET);
		for (j=0; j<streamsize[i]; j++)
		{
			fread(&value, 4, 1, bitfile); 
			fwrite(&value, 4, 1, outfile);
		}
		fclose(bitfile);
		
		// write setout
		
		if (i<num-1)
		{
			// prevent device from startup and activate dout pin
			// see configuration guide for the meaning of the commands
			fwriteswap(outfile, 0xaa995566);
			fwriteswap(outfile, 0x30008001); fwriteswap(outfile, 0x0000000b);
			fwriteswap(outfile, 0x30008001); fwriteswap(outfile, 0x00000007);
			fwriteswap(outfile, 0x00000000);
			fwriteswap(outfile, 0x00000000);
			fwriteswap(outfile, 0x30010000); // enable dout
			
			// sent data via dout to the downstream devices
			size = (num-i-1)*4+(num-i-2)*(8+1+8);
			for(j=i+1;j<num;j++) size+= streamsize[j];
			fwriteswap(outfile, 0x50000000 | size);
		}
	}
	
	// startup devices and write padding
	
	for(i=num; i>0; i--)
	{
		if(i<num)
		{
			// startup sequence
			fwriteswap(outfile, 0x30008001); fwriteswap(outfile, 0x00000005);
			fwriteswap(outfile, 0x30008001); fwriteswap(outfile, 0x0000000d);
			
			fwriteswap(outfile, 0x0);
			fwriteswap(outfile, 0x0);
			fwriteswap(outfile, 0x0);
			fwriteswap(outfile, 0x0);
		}
		
		if (i>1) 
		{
			// trail padding between bistreams
			fwriteswap(outfile, 0x0);
			fwriteswap(outfile, 0x0);
			fwriteswap(outfile, 0x0);
			fwriteswap(outfile, 0x0);
		}
	}
	
	fclose(outfile);
	return(0);
} 
