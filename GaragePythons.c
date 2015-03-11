#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>
#include <jerror.h>
#include <openssl/sha.h>

// constants for the embed/extract mode
#define EMBED   1
#define EXTRACT 2

void main(int argc, char **argv)
{
	// determines whether twe are embedding or extracting
	int mode;

	// types for the libjpeg input object
	struct jpeg_decompress_struct cinfo_in;
	struct jpeg_error_mgr jpegerr_in;
	jpeg_component_info *component;
	jvirt_barray_ptr *DCT_blocks;

	// types for the libjpeg output objet
	struct jpeg_compress_struct cinfo_out;
	struct jpeg_error_mgr jpegerr_out;

	// file handles
	FILE *file_in;
	FILE *file_out;     // only used for embedding
	FILE *file_payload; // only used for embedding

	// to store the payload
	unsigned long payload_length; 
	unsigned char *payload_bytes;
	unsigned char *payload_bits;
	unsigned long BUFFSIZE=1024*1024; //1MB hardcoded max payload size, plenty

	// the key string, and its SHA-1 hash
	char *key;
	unsigned char *keyhash;

	// useful properties of the image
	unsigned long blocks_high, blocks_wide;

	// for the example code
	int block_y, block_x, u, v;

	// parse parameters
	if(argc==4 && strcmp(argv[1],"embed")==0)
	{
		mode=EMBED;
		key=argv[3];
	}
	else if(argc==3 && strcmp(argv[1],"extract")==0)
	{
		mode=EXTRACT;
		key=argv[2];
	}
	else
	{
		fprintf(stderr, "Usage: GaragePythons embed cover.jpg key <payload >stego.jpg\n");
		fprintf(stderr, "Or     GaragePythons extract key <stego.jpg\n");
		exit(1);
	}

	if(mode==EMBED)
	{
		// read ALL (up to eof, or max buffer size) of the payload into the buffer
		if((payload_bytes=malloc(BUFFSIZE))==NULL)
		{
			fprintf(stderr, "Memory allocation failed!\n");
			exit(1);
		}
		file_payload=stdin;
		payload_length=fread(payload_bytes, 1, BUFFSIZE, file_payload);
		fprintf(stderr, "Embedding payload of length %ld bytes...\n", payload_length);

		if((payload_bits=malloc(payload_length*8 + 64))==NULL)
		{
			fprintf(stderr, "Memory allocation failed!\n");
			exit(1);
		}

		int i=0;
		for (i=0; i<64; i++) {
			const char b = ( payload_length & ( 1<< i ) ) != 0;
			payload_bits[i]=b;
		}

		size_t charpos = 0;
		unsigned int bitpos = 128;

		while( charpos < payload_length ) {
			const char b = ( payload_bytes[ charpos ] & ( bitpos ) ) != 0;
			payload_bits[i++] = b;
			bitpos >>= 1;
			if( bitpos == 0 ) {
				bitpos = 128;
				charpos++;
			}
		}
		for (i=64; i<128; i++) {
				fprintf(stderr, "%d", payload_bits[i]);

		}
		fprintf(stderr, "\n");

	}  	

	// open the input file
	if(mode==EMBED)
	{
		if((file_in=fopen(argv[2],"rb"))==NULL)
		{
			fprintf(stderr, "Unable to open cover file %s\n", argv[2]);
			exit(1);
		}
	}
	else if(mode==EXTRACT)
	{
		file_in=stdin;
	}

	// libjpeg -- create decompression object for reading the input file, using the standard error handler
	cinfo_in.err = jpeg_std_error(&jpegerr_in);
	jpeg_create_decompress(&cinfo_in);

	// libjpeg -- feed the cover file handle to the libjpeg decompressor
	jpeg_stdio_src(&cinfo_in, file_in);

	// libjpeg -- read the compression parameters and first (luma) component information
	jpeg_read_header(&cinfo_in, TRUE);
	component=cinfo_in.comp_info;

	// these are very useful (they apply to luma component only)
	blocks_wide=component->width_in_blocks;
	blocks_high=component->height_in_blocks;
	// these might also be useful:
	// component->quant_table->quantval[i] gives you the quantization factor for code i (i=0..63, scanning the 8x8 modes in row order)

	// libjpeg -- read all the DCT coefficients into a memory structure (memory handling is done by the library)
	DCT_blocks=jpeg_read_coefficients(&cinfo_in);

	// if embedding, set up the output file
	// (we had to read the input first so that libjpeg can set up an output file with the exact same parameters)
	if(mode==EMBED)
	{
		// libjpeg -- create compression object with default error handler
		cinfo_out.err = jpeg_std_error(&jpegerr_out);
		jpeg_create_compress(&cinfo_out);

		// libjpeg -- copy all parameters from the input to output object
		jpeg_copy_critical_parameters(&cinfo_in, &cinfo_out);

		// libjpeg -- feed the stego file handle to the libjpeg compressor
		file_out=stdout;
		jpeg_stdio_dest(&cinfo_out, file_out);
	}


	// At this point the input has been read, and an output is ready (if embedding)
	// We can modify the DCT_blocks if we are embedding, or just print the payload if extracting

	if((keyhash=malloc(20))==NULL) // enough space for a 160-bit hash
	{
		fprintf(stderr, "Memory allocation failed!\n");
		exit(1);
	}
	SHA1(key, strlen(key), keyhash);
	const unsigned int max = blocks_high*blocks_wide*64;

	unsigned long* positions;
	if((positions=malloc(max*sizeof(unsigned long)))==NULL)
	{
		fprintf(stderr, "Memory allocation failed!\n");
		exit(1);
	}


	for (unsigned int j=0; j<max; j++) {
		positions[j]=j;
	}
	srand(*(unsigned int *)keyhash);

	void shuffle(unsigned long* a, int m)
	{
		for (unsigned int k=m-1; k>0; k--)  {
			unsigned int j = (int)((double)k * ( rand() / (RAND_MAX+1.0) ));
			unsigned int temp = a[k];
			a[k]=a[j];
			a[j]=temp;

		}

	}


	shuffle(positions,max);
	fprintf(stderr, "Shuffling %d positions...\n", max);
	/*
	   fprintf(stderr, "Shuffle: ");
	   for (int i = 0; i < max; i++)
	   fprintf(stderr," %d", positions[i]);
	   fprintf(stderr, "\n");
	   */

	
	int* values;
	if((values=malloc(max*sizeof(int)))==NULL)
	{
		fprintf(stderr, "Memory allocation failed!\n");
		exit(1);
	}   
	
	
	int k=0; 
	for (block_y=0; block_y<component->height_in_blocks; block_y++)
	{
		for (block_x=0; block_x< component->width_in_blocks; block_x++)
		{
			// this is the magic code which accesses block (block_x,block_y) from the luma component of the image
			JCOEFPTR block=(cinfo_in.mem->access_virt_barray)((j_common_ptr)&cinfo_in, DCT_blocks[0], block_y, (JDIMENSION)1, FALSE)[0][block_x];
			
			for (u=0; u<8; u++)
			{
				for(v=0; v<8; v++)
				{
					values[k]=block[u*8+v];
					k++;
				}

			}
		}
	}

	fprintf(stderr, "Adding values in  ...\n");
	
	int size=payload_length*8+sizeof(payload_length)*8;
	fprintf(stderr, "%d siiiize ", size);
	if(mode==EMBED)
	{
		int p=0;
		k=0;
		
		//fprintf(stderr, " size %3d ", size);
		while (p < size && k < max) {
			unsigned long pos = positions[k];	
			k++;
			if (values[pos] != 0) {
				if ((payload_bits[p]%2)*(payload_bits[p]%2) != (values[pos]%2)*(values[pos]%2)) {	
					if (values[pos] > 0) values[pos]--;
					else values[pos]++;
				}
				if (values[pos]!=0) p++;
			}

		}


		k=0;
		for (block_y=0; block_y<component->height_in_blocks; block_y++)
		{
			for (block_x=0; block_x< component->width_in_blocks; block_x++)
			{
				// this is the magic code which accesses block (block_x,block_y) from the luma component of the image
				JCOEFPTR block=(cinfo_in.mem->access_virt_barray)((j_common_ptr)&cinfo_in, DCT_blocks[0], block_y, (JDIMENSION)1, FALSE)[0][block_x];
				
				for (u=0; u<8; u++)
				{
					for(v=0; v<8; v++)
					{
						block[u*8+v]=values[k];
						k++;

					}

				}

			}
		}


		// libjpeg -- write the coefficient block
		jpeg_write_coefficients(&cinfo_out, DCT_blocks);
		jpeg_finish_compress(&cinfo_out);
	}
	else if(mode==EXTRACT)
	{	
		
		int j=0;
		int length = 0; 
		int factor = 1;
		int l = 64;
		int p=0;
		// get payload length 
		while (j<l) {
			if (values[positions[j]]!=0) {			
				length+=((values[positions[j]]%2)*(values[positions[j]]%2)*(factor));
				factor=factor*2;				
				
					
			}
			else
				l++;
			j++;
		}
		
		fprintf(stderr, "Got length %d\n", length);
		if((payload_bits=malloc(BUFFSIZE))==NULL)
		{
			fprintf(stderr, "Memory allocation failed!\n");
			exit(1);
		}
		//int lim=p;
		p=0;
		fprintf(stderr, " payload_bits size %d", length*8);
		while (p<length*8 && j<max) {
			if (values[positions[j]]!=0) {
				payload_bits[p]=(values[positions[j]]%2)*(values[positions[j]]%2);
				p++;
			}
			j++;
		
		}
		
		for (int i=0; i<64; i++)
			fprintf(stderr, "%d", payload_bits[i]);
		fprintf(stderr, "\n");	
		if((payload_bytes=malloc(BUFFSIZE))==NULL)
		{
			fprintf(stderr, "Memory allocation failed!\n");
			exit(1);
		}
		
		for( size_t i = 0; i < length; i++ ) {
			payload_bytes[i] = 0;
			for( int b = 0; b < 8; b++ ) {
				payload_bytes[i] |= payload_bits[i*8+b] << (7-b);
				
			}
		}
		
		printf("%s\n", payload_bytes);
		
		
		
	}


	// example code: prints out all the DCT blocks to stderr, scanned in row order, but does not change them
	// (if "embedding", the cover jpeg was also sent unchanged  to stdout)

	
	// libjpeg -- finish with the input file and clean up
	jpeg_finish_decompress(&cinfo_in);
	jpeg_destroy_decompress(&cinfo_in);

	// free memory blocks (not actually needed, the OS will do it)
	free(keyhash);
	free(payload_bytes);	

}
