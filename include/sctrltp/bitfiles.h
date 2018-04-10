#ifndef _H_BIT2BST
#define _H_BIT2BST
/* This functions were cut from config.cpp in fpgasystem/sw/pc/prog */

int fwriteswap(FILE* file, unsigned int l);
int ParseBitfile(char* filename, long int* ofs, int verbose);
int CreateSerialBitstream(char* outfilename, char** bitfilenames, int num);


#endif /* _H_BIT2BST */
