/*
MIT LICENSE

Copyright 2014 Inertial Sense, LLC - http://inertialsense.com

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files(the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef DATA_CVS_H
#define DATA_CVS_H

#include <string>
#include <map>
#include <regex>

#include "com_manager.h"

#ifdef USE_IS_INTERNAL
#include "../../libs/IS_internal.h"
#endif

using namespace std;

class cDataCSV
{
public:
	int WriteHeaderToFile(FILE* pFile, int id);
	int ReadHeaderFromFile(FILE* pFile, int id, vector<string>& columnHeaders);
	int WriteDataToFile(uint64_t orderId, FILE* pFile, const p_data_hdr_t& dataHdr, uint8_t *dataBuf);

	/*!
	* Parse a csv string into a data packet
	* data needs the id set to the proper data id
	* buf is assumed to be large enough to hold the data structure
	* order id contains the value for ordering data
	* returns true if success, false if no map found
	*/
	bool StringCSVToData(string& s, p_data_hdr_t& hdr, uint8_t* buf, const vector<string>& columnHeaders);

	/*!
	* Convert data to a csv string
	* buf is assumed to be large enough to hold the data structure
	* return true if success, false if no map found
	*/
	bool DataToStringCSV(const p_data_hdr_t& hdr, const uint8_t* buf, string& csv);
};

#endif // DATA_CVS_H
