#include "stdafx.h"
#include <Windows.h>
#include <stdint.h>
#include <vector>
#include <string.h>
#include <winsock.h>

// Include namespace std by default.
using namespace std;

#pragma region ImageAnalysis

typedef struct _TiffHeader
{
	WORD  Identifier;  // Byte-order Identifier
	WORD  Version;     // TIFF version number (always 2Ah)
	DWORD IFDOffset;   // Offset of the first Image File Directory
} TIFHEAD;

typedef struct _TifTag
{
	WORD   TagId;       // The tag identifier
	WORD   DataType;    // The scalar type of the data items
	DWORD  DataCount;   // The number of items in the tag data
	DWORD  DataOffset;  // The byte offset to the data items
} TIFTAG;

#define MOTOROLA_BYTE_ORDER     0x4D4D
#define INTEL_BYTE_ORDER        0x4949

#define TAG_IMAGE_WIDTH         256
#define TAG_IMAGE_HEIGHT        257

#define PNG_WIDTH_OFFSET        16
#define PNG_HEIGHT_OFFSET       20

#define GIF_WIDTH_OFFSET        6
#define GIF_HEIGHT_OFFSET       8

#define BMP_HEADER_LENGTH_OFFSET 14  // Offset of bitmap in file - BOF
#define BMP_WIN_HEADER_LENGTH   40    // Windows 3.x format
#define BMP_OS2_HEADER_LENGTH   20    // OS/2 format
#define BMP_WIDTH_OFFSET        18
#define BMP_WIN_HEIGHT_OFFSET   22    // For Windows header format
#define BMP_OS2_HEIGHT_OFFSET   20    // For OS/2 header format
#define NULL_STR                "\x0\x0\x0\x0"

// Frame start marker
#define SOF0        0xC0

// Define Huffman Tables marker - DHT - Variable size payload
#define DHT         0xC4

// Define arithmetic coding conditioning(s) marker - DAC - Variable size payload
#define DAC         0xCC

//  Define Quantization Tables marker - DQT - Variable size payload
#define DQT         0xDB

//  Define Restart Interval marker - DRI - 4 bytes payload
#define DRI         0xDD

//  Comment marker - COM - variable size payload
#define COM         0xFE

// Application defined markers - APPn (n=0...15) - Variable size payload
#define APP0        0xE0
#define APP1        0xE1
#define APP2        0xE2
#define APP3        0xE3
#define APP4        0xE4
#define APP5        0xE5
#define APP6        0xE6
#define APP7        0xE7
#define APP8        0xE8
#define APP9        0xE9
#define APPA        0xEA
#define APPB        0xEB
#define APPC        0xEC
#define APPD        0xED
#define APPE        0xEE
#define APPF        0xEF

#define EXT_TIFF    L".tiff"
#define EXT_PNG     L".png"
#define EXT_GIF     L".gif"
#define EXT_BMP     L".bmp"
#define EXT_JPG     L".jpg"
#define EXT_JPEG    L".jpeg"

unsigned long GetLongField(unsigned char *buff, DWORD numberType)
{
	if (numberType != MOTOROLA_BYTE_ORDER)
	{
		// little-endian (Intel format)
		return((unsigned long)((unsigned long)buff[0] & 0xff) +
			(((unsigned long)buff[1] & 0xff) << 8) +
			(((unsigned long)buff[2] & 0xff) << 16) +
			(((unsigned long)buff[3] & 0xff) << 24)
			);
	}
	else
	{
		// big-endian (Motorola format)
		return((unsigned long)(((unsigned long)buff[0] & 0xff) << 24) ||
			(((unsigned long)buff[1] & 0xff) << 16) ||
			(((unsigned long)buff[2] & 0xff) << 8) ||
			(((unsigned long)buff[3] & 0xff))
			);
	}
}

/*
@brief - This function allows to get the file data with the given limit of the size.
@Param - filepath: path of the file for which to get the data.
@Param - limit: specifies the length of the data to get from the file.
@Returns - vector of the uint8_t filled with file data of size limit or the actual read length - whichever is lesser.
*/
std::vector<uint8_t> GetFileBuffer(const wchar_t* filepath, UINT limit)
{
	std::vector<uint8_t> v(limit, 0);
	FILE *fp = NULL; // file descriptor
	int actualRead = 0;

	if ((_wfopen_s(&fp, filepath, L"rb") == 0) && fp != NULL)
	{
		actualRead = (int)fread((unsigned char*)v.data(), sizeof(unsigned char), limit, fp);
		fclose(fp);

		// resize the vector to the size of actual read elements.
		if (actualRead)
			v.resize(actualRead);
	}
	return v;
}

/*
@brief - This function scans the image headers to read image dimensions.
@Param - ext: extension of the image file.
@Param - data: Buffer to scan dimensions in.
@Param - length: length of the data.
@Param - width: X-Resolution of the image.
@Param - height: Y-Resolution of the image.
@Returns - true if we get the dimensions otherwise false.
@Returns - If it returns false then, either there are not enough data byttes available to get image dimensions or there is a bug.
*/
bool ReadImageDimensions(const wchar_t* ext, void* data, int length, UINT &width, UINT &height)
{
	bool success = false;
	unsigned char *buffer = (unsigned char *)data;

#pragma region TIFF
	// compare file headers to determine the file type
	if (!_wcsicmp(ext, EXT_TIFF))
	{
		TIFHEAD head; TIFTAG tag;
		unsigned long offset = 0;
		unsigned short numDirEntries = 0;

		if (length <= sizeof(TIFHEAD))
			return false; // Not enough bytes available

		memcpy(&head, buffer, sizeof(TIFHEAD));
		if (head.Identifier == MOTOROLA_BYTE_ORDER || head.Identifier == INTEL_BYTE_ORDER)
		{
			offset = GetLongField((unsigned char *)&head.IFDOffset, head.Identifier);
			if ((unsigned long)length <= offset)
				return false;

			unsigned char* tifdata = buffer + offset;
			memcpy((char*)&numDirEntries, tifdata, sizeof(short));
			for (int i = 0, next = 0; i < (int)numDirEntries; i++)
			{
				if ((unsigned long)length <= (offset + next + sizeof(tag)))
					break;

				memcpy(&tag, tifdata + next + 2, sizeof(tag)); // Skip numDirEntries entry
															   /*
															   If the tag data is four bytes or less in size, the data may be found in this field.
															   If the tag data is greater than four bytes in size, then this field contains an offset to the position of the data in the TIFF file.
															   */
				if (tag.TagId == TAG_IMAGE_WIDTH) // width
				{
					if (tag.DataType <= sizeof(long))
					{
						//width = tag.DataOffset;
						width = (UINT)GetLongField((unsigned char *)&tag.DataOffset, head.Identifier);
						success = true;
					}
				}
				else if (tag.TagId == TAG_IMAGE_HEIGHT) // height
				{
					if (tag.DataType <= sizeof(long))
					{
						//height = tag.DataOffset;
						height = (UINT)GetLongField((unsigned char *)&tag.DataOffset, head.Identifier);
						success = true;
					}
				}
				next += sizeof(tag);
			}
		}
	}
#pragma endregion

#pragma region PNG
	else if (!_wcsicmp(ext, EXT_PNG))
	{
		/*
		Offset(0) - 8 bytes - Signature: 89h 50h 4Eh 47h 0Dh 0Ah 1Ah 0Ah = "\x89PNG\r\n\x1A\n"
		Offset(8) - 4 bytes - Data length of the first PNG chunk.
		Offset(12) - 4 bytes - Code identifying the type of chunk. IHDR must be the first chunk following the 8-byte signature.
		Offset(16) - 4 bytes - Width of image in pixels.
		Offset(20) - 4 bytes - Height of image in pixels.
		*/

		// PNG - Numerical Format = Big-endian (Property Long)
		if (length >= (PNG_HEIGHT_OFFSET + sizeof(long)))
		{
			memcpy((char *)&width, buffer + PNG_WIDTH_OFFSET, sizeof(long));
			memcpy((char *)&height, buffer + PNG_HEIGHT_OFFSET, sizeof(long));

			// convert to host byte order
			width = ntohl(width);
			height = ntohl(height);
			success = true;
		}
	}
#pragma endregion

#pragma region GIF
	else if (!_wcsicmp(ext, EXT_GIF))
	{
		// GIF - Numerical Format = Little-endian (Property Short)
		if (length >= (GIF_HEIGHT_OFFSET + sizeof(short)))
		{
			memcpy((char *)&width, buffer + GIF_WIDTH_OFFSET, sizeof(short));
			memcpy((char *)&height, buffer + GIF_HEIGHT_OFFSET, sizeof(short));
			success = true;
		}
	}
#pragma endregion

#pragma region BMP
	else if (!_wcsicmp(ext, EXT_BMP))
	{
		/*
		Offset(0)  - 2 bytes - Header Field (e.g. BM, BI, CI, CP, IC, PT).
		Offset(2)  - 4 bytes - The size of the BMP file in bytes.
		Offset(8)  - 2 bytes - Reserved.
		Offset(8)  - 2 bytes - Reserved.
		Offset(10) - 4 bytes - The offset, i.e. starting address, of the byte where the bitmap image data (pixel array) can be found.
		Offset(14) - 4 bytes - The size of the header.
		*/
		if (length < 10)
			return false;

		if (!memcmp(buffer + 6, NULL_STR, 4)) // 7 to 10 bytes must be zero
		{
			if (length < (BMP_HEADER_LENGTH_OFFSET + sizeof(long)))
				return false;

			UINT headerLength = 0;
			memcpy((char *)&headerLength, buffer + BMP_HEADER_LENGTH_OFFSET, sizeof(long));
			if (headerLength == BMP_WIN_HEADER_LENGTH)
			{
				// BITMAP - Windows - Numerical Format = Little-endian (Property Long)
				if (length >= (BMP_WIN_HEIGHT_OFFSET + sizeof(long)))
				{
					memcpy((char *)&width, buffer + BMP_WIDTH_OFFSET, sizeof(long));
					memcpy((char *)&height, buffer + BMP_WIN_HEIGHT_OFFSET, sizeof(long));
					success = true;
				}
			}
			else if (headerLength == BMP_OS2_HEADER_LENGTH)
			{
				// BITMAP - OS/2 - Numerical Format = Little-endian (Property Short)
				if (length >= (BMP_OS2_HEIGHT_OFFSET + sizeof(short)))
				{
					memcpy((char *)&width, buffer + BMP_WIDTH_OFFSET, sizeof(short));
					memcpy((char *)&height, buffer + BMP_OS2_HEIGHT_OFFSET, sizeof(short));
					success = true;
				}
			}
		}
	}
#pragma endregion

#pragma region JPEG
	else if (!_wcsicmp(ext, EXT_JPG) || !_wcsicmp(ext, EXT_JPEG))
	{
		/*
		@brief - This section skips all the interpret markers as shown in table below which comes before the actual SOFn.
		--------------------------------------
		Marker      Purpose
		--------------------------------------
		DHT     Define Huffman Tables
		DAC     Define Arithmetic Conditioning
		DQT     Define Quantization Tables
		DRI     Define Restart Interval
		APPn    Application defined marker
		COM     Comment
		--------------------------------------
		*/

		for (int i = 2; i < length - 1;)
		{
			uint8_t value = (uint8_t)buffer[i + 1];
			if (uint8_t(buffer[i]) == (uint8_t)0xFF) // Segment Start Marker
			{
				UINT skipBytes = 0;
				switch (value)
				{
				case uint8_t(DHT):
				case uint8_t(DAC):
				case uint8_t(DQT):
				case uint8_t(COM):
					// Variable size payload markers

				case uint8_t(APP0):
				case uint8_t(APP1):
				case uint8_t(APP2):
				case uint8_t(APP3):
				case uint8_t(APP4):
				case uint8_t(APP5):
				case uint8_t(APP6):
				case uint8_t(APP7):
				case uint8_t(APP8):
				case uint8_t(APP9):
				case uint8_t(APPA):
				case uint8_t(APPB):
				case uint8_t(APPC):
				case uint8_t(APPD):
				case uint8_t(APPE):
				case uint8_t(APPF):
					// Variable size payload APPn markers
					if (i + 3 >= length) return false;

					skipBytes = (buffer[i + 2] << 8) + buffer[i + 3];
					i += skipBytes + 2; // Skip the marker, as it excluded in the segment length.

					break;
				case uint8_t(DRI):
					// 4 bytes payload marker
					i += 4 + 2;

					if (i >= length) return false;
					break;
				case uint8_t(SOF0):
					if (i + 8 >= length) return false;

					/*
					Offset(0) - 2 bytes - SOF0: 0xFFC0
					Offset(2) - 2 bytes - Frame header length
					Offset(4) - 1 byte - Sample precision
					Offset(5) - 2 bytes - Height
					Offset(7) - 2 bytes - Width
					*/
					width = (buffer[i + 7] << 8) + buffer[i + 8];
					height = (buffer[i + 5] << 8) + buffer[i + 6];
					return true;
				default:
					i++; // This should never execute.
					break;
				}
			}
			else
			{
				i++; // This should never execute.
			}
		}
	}
#pragma endregion
	return success;
}

#pragma endregion