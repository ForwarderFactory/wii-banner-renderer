// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "CommonFuncs.h"
#include <strings.h>
#include "FileHandlerARC.h"
//#include "StringUtil.h"
#include "Blob.h"
#include <fstream>

// hacks
//#include "../../../Source/Endian.h"
#include <../../include/libwb/Endian.h>

#define ARC_ID 0x55aa382d

namespace DiscIO
{

CARCFile::CARCFile(std::istream& file)
    : m_pBuffer(NULL)
    , m_Initialized(false)
{
	// get size, lame;
	//file.seekg(0, std::ios::end);
	//u64 file_size = file.tellg();

   // m_pBuffer = new u8[file_size];
	//file.seekg(0, std::ios::beg);
    //file.read((char*)m_pBuffer, file_size);

    //m_Initialized = ParseBuffer();
	m_Initialized = ParseBuffer(file);
}

CARCFile::CARCFile(const std::string& _rFilename)
    : m_pBuffer(NULL)
    , m_Initialized(false)
{
    DiscIO::IBlobReader* pReader = DiscIO::CreateBlobReader(_rFilename.c_str());
    if (pReader != NULL)
    {
        u64 FileSize = pReader->GetDataSize();
        m_pBuffer = new u8[(u32)FileSize];
        pReader->Read(0, FileSize, m_pBuffer);
        delete pReader;

        m_Initialized = ParseBuffer();
    }
}

CARCFile::CARCFile(const std::string& _rFilename, u32 offset)
	: m_pBuffer(NULL)
	, m_Initialized(false)
{
	DiscIO::IBlobReader* pReader = DiscIO::CreateBlobReader(_rFilename.c_str());
	if (pReader != NULL)
	{
		u64 FileSize = pReader->GetDataSize() - offset;
		m_pBuffer = new u8[(u32)FileSize];
		pReader->Read(offset, FileSize, m_pBuffer);
		delete pReader;

		m_Initialized = ParseBuffer();
	}
}

CARCFile::CARCFile(const u8* _pBuffer, size_t _BufferSize)
	: m_pBuffer(NULL)
	, m_Initialized(false)
{
	m_pBuffer = new u8[_BufferSize];

	if (m_pBuffer)
	{
		memcpy(m_pBuffer, _pBuffer, _BufferSize);
		m_Initialized = ParseBuffer();
	}
}


CARCFile::~CARCFile()
{
	delete [] m_pBuffer;
}


bool
CARCFile::IsInitialized()
{
	return(m_Initialized);
}


size_t
CARCFile::GetFileSize(const std::string& _rFullPath) const
{
	if (!m_Initialized)
	{
		return(0);
	}

	const SFileInfo* pFileInfo = FindFileInfo(_rFullPath);

	if (pFileInfo != NULL)
	{
		return((size_t) pFileInfo->m_FileSize);
	}

	return(0);
}


size_t CARCFile::GetFileOffset(const std::string& _rFullPath) const
{
	if (!m_Initialized)
		return 0;

	const SFileInfo* const pFileInfo = FindFileInfo(_rFullPath);

	if (pFileInfo)
		return (size_t)pFileInfo->m_Offset;

	return(0);
}

size_t
CARCFile::ReadFile(const std::string& _rFullPath, u8* _pBuffer, size_t _MaxBufferSize)
{
	if (!m_Initialized)
	{
		return(0);
	}

	const SFileInfo* pFileInfo = FindFileInfo(_rFullPath);

	if (pFileInfo == NULL)
	{
		return(0);
	}

	if (pFileInfo->m_FileSize > _MaxBufferSize)
	{
		return(0);
	}

	memcpy(_pBuffer, &m_pBuffer[pFileInfo->m_Offset], (size_t)pFileInfo->m_FileSize);
	return((size_t) pFileInfo->m_FileSize);
}


bool
CARCFile::ExportFile(const std::string& _rFullPath, const std::string& _rExportFilename)
{
	if (!m_Initialized)
	{
		return(false);
	}

	const SFileInfo* pFileInfo = FindFileInfo(_rFullPath);

	if (pFileInfo == NULL)
	{
		return(false);
	}

	FILE* pFile = fopen(_rExportFilename.c_str(), "wb");

	if (pFile == NULL)
	{
		return(false);
	}

	fwrite(&m_pBuffer[pFileInfo->m_Offset], (size_t) pFileInfo->m_FileSize, 1, pFile);
	fclose(pFile);
	return(true);
}


bool
CARCFile::ExportAllFiles(const std::string&)
{
	return(false);
}

bool CARCFile::ParseBuffer(std::istream& file)
{
	// support reading from the middle of an stream
	const std::streamoff file_start = file.tellg();

	// check ID
	u32 id;
	file >> wb::BE >> id; // hack maybe we should just get rid of this file and all this circa 2008 dolphin shit

	if (id != ARC_ID)
		return false;

	// read header
	u32 fst_offset, fst_size, file_offset;
	file >> wb::BE >> fst_offset >> fst_size >> file_offset;

	file.seekg(file_start + fst_offset, std::ios::beg);

	// read all file infos
	u32 root_name_offset, root_offset, root_filesize;
	file >> wb::BE >> root_name_offset >> root_offset >> root_filesize;

	SFileInfo Root;
	Root.m_NameOffset = root_name_offset;
	Root.m_Offset = root_offset;
	Root.m_FileSize = root_filesize;

	if (Root.IsDirectory())
	{
		m_FileInfoVector.resize(Root.m_FileSize);
		const u32 name_table_offset = fst_offset + (0xC * Root.m_FileSize);

		file.seekg(file_start + fst_offset, std::ios::beg);

		for (size_t i = 0; i < m_FileInfoVector.size(); ++i)
		{
			//u8* Offset = m_pBuffer + fst_offset + (i * 0xC);

			u32 name_offset, offset, filesize;
			file >> wb::BE >> name_offset >> offset >> filesize;

			m_FileInfoVector[i].m_NameOffset = name_offset;
			m_FileInfoVector[i].m_Offset = offset;
			m_FileInfoVector[i].m_FileSize = filesize;
		}

		file.seekg(file_start + name_table_offset, std::ios::beg);
		BuildFilenames(1, m_FileInfoVector.size(), file);
	}

	return(true);
}

bool
CARCFile::ParseBuffer()
{
	// check ID
	u32 ID = Common::swap32(*(u32*)(m_pBuffer));

	if (ID != ARC_ID)
		return false;

	// read header
	u32 FSTOffset  = Common::swap32(*(u32*)(m_pBuffer + 0x4));
	//u32 FSTSize    = Common::swap32(*(u32*)(m_pBuffer + 0x8));
	//u32 FileOffset = Common::swap32(*(u32*)(m_pBuffer + 0xC));

	// read all file infos
	SFileInfo Root;
	Root.m_NameOffset = Common::swap32(*(u32*)(m_pBuffer + FSTOffset + 0x0));
	Root.m_Offset     = Common::swap32(*(u32*)(m_pBuffer + FSTOffset + 0x4));
	Root.m_FileSize   = Common::swap32(*(u32*)(m_pBuffer + FSTOffset + 0x8));

	if (Root.IsDirectory())
	{
		m_FileInfoVector.resize(Root.m_FileSize);
		const char* szNameTable = (char*)(m_pBuffer + FSTOffset);

		for (size_t i = 0; i < m_FileInfoVector.size(); i++)
		{
			u8* Offset = m_pBuffer + FSTOffset + (i * 0xC);
			m_FileInfoVector[i].m_NameOffset = Common::swap32(*(u32*)(Offset + 0x0));
			m_FileInfoVector[i].m_Offset   = Common::swap32(*(u32*)(Offset + 0x4));
			m_FileInfoVector[i].m_FileSize = Common::swap32(*(u32*)(Offset + 0x8));

			szNameTable += 0xC;
		}

		BuildFilenames(1, m_FileInfoVector.size(), NULL, szNameTable);
	}

	return(true);
}

size_t CARCFile::BuildFilenames(const size_t first_index, const size_t last_index,
	std::istream& file, const char* directory)
{
	// support reading from the middle of an stream
	const std::streamoff file_start = file.tellg();

	size_t current_index = first_index;
	while (current_index != last_index)
	{
		SFileInfo& rFileInfo = m_FileInfoVector[current_index];
		const u32 offset = rFileInfo.m_NameOffset & 0xFFFFFF;

		file.seekg(file_start + offset, std::ios::beg);

		char name[512];
		file.getline(name, sizeof(name), '\0');

		// check next index
		if (rFileInfo.IsDirectory())
		{
			// this is a directory, build up the new szDirectory
			sprintf(rFileInfo.m_FullPath, "%s%s/", directory, name);

			file.seekg(file_start, std::ios::beg);
			current_index = BuildFilenames(current_index + 1, rFileInfo.m_FileSize, file, rFileInfo.m_FullPath);
		}
		else
		{
			// this is a filename
			sprintf(rFileInfo.m_FullPath, "%s%s", directory, name);

			++current_index;
		}
	}

	return current_index;
}

size_t
CARCFile::BuildFilenames(const size_t _FirstIndex, const size_t _LastIndex, const char* _szDirectory, const char* _szNameTable)
{
	size_t CurrentIndex = _FirstIndex;

	while (CurrentIndex < _LastIndex)
	{
		SFileInfo& rFileInfo = m_FileInfoVector[CurrentIndex];
		int uOffset = rFileInfo.m_NameOffset & 0xFFFFFF;

		// check next index
		if (rFileInfo.IsDirectory())
		{
			// this is a directory, build up the new szDirectory
			if (_szDirectory != NULL)
			{
				sprintf(rFileInfo.m_FullPath, "%s%s/", _szDirectory, &_szNameTable[uOffset]);
			}
			else
			{
				sprintf(rFileInfo.m_FullPath, "%s/", &_szNameTable[uOffset]);
			}

			CurrentIndex = BuildFilenames(CurrentIndex + 1, (size_t) rFileInfo.m_FileSize, rFileInfo.m_FullPath, _szNameTable);
		}
		else
		{
			// this is a filename
			if (_szDirectory != NULL)
			{
				sprintf(rFileInfo.m_FullPath, "%s%s", _szDirectory, &_szNameTable[uOffset]);
			}
			else
			{
				sprintf(rFileInfo.m_FullPath, "%s", &_szNameTable[uOffset]);
			}

			CurrentIndex++;
		}
	}

	return(CurrentIndex);
}


const SFileInfo*
CARCFile::FindFileInfo(std::string _rFullPath) const
{
	for (size_t i = 0; i < m_FileInfoVector.size(); i++)
	{
		if (!strcasecmp(m_FileInfoVector[i].m_FullPath, _rFullPath.c_str()))
		{
			return(&m_FileInfoVector[i]);
		}
	}

	return(NULL);
}
} // namespace
