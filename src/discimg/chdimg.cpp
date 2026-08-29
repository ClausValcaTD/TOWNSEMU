#include "chdimg.h"
#include <libchdr/chd.h>
#include <libchdr/cdrom.h>
#include <cstring>
#include <algorithm>
#include <cstdio>

CHDImage::CHDImage()
{
}

CHDImage::~CHDImage()
{
    Close();
}

void CHDImage::Close(void)
{
    if(nullptr != chd)
    {
        chd_close(chd);
        chd = nullptr;
    }
    tracks.clear();
    regions.clear();
    numSectors = 0;
    imageSize = 0;
    framesPerHunk = 0;
    hunkBuf.clear();
    hunkInBuf = ~0u;
}

bool CHDImage::IsOpen(void) const
{
    return chd != nullptr;
}

unsigned int CHDImage::Open(const std::string &fName)
{
    Close();

    chd_error err = chd_open(fName.c_str(), CHD_OPEN_READ, nullptr, &chd);
    if(CHDERR_NONE != err || nullptr == chd)
    {
        chd = nullptr;
        return CHDERROR_CANNOT_OPEN;
    }

    const chd_header *header = chd_get_header(chd);
    if(nullptr == header)
    {
        Close();
        return CHDERROR_CANNOT_OPEN;
    }

    if(header->unitbytes != FRAME_SIZE && header->unitbytes != SECTOR_DATA_SIZE)
    {
        Close();
        return CHDERROR_NOT_A_CD;
    }

    framesPerHunk = header->hunkbytes / header->unitbytes;
    if(0 == framesPerHunk)
    {
        Close();
        return CHDERROR_NOT_A_CD;
    }

    hunkBuf.resize(header->hunkbytes);
    hunkInBuf = ~0u;

    unsigned int res = MakeTrackTable();
    if(CHDERROR_NOERROR != res)
    {
        Close();
        return res;
    }

    return CHDERROR_NOERROR;
}

unsigned int CHDImage::MakeTrackTable(void)
{
    tracks.clear();
    regions.clear();

    unsigned int currentLBA = 0;
    unsigned int currentCHDFrame = 0;
    uint64_t currentLocInImg = 0;

    for(int i = 0; ; ++i)
    {
        char meta[512] = {0};
        uint32_t actlen = 0;
        chd_error err = chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, i, meta, sizeof(meta) - 1, &actlen, nullptr, nullptr);
        if(CHDERR_NONE != err)
        {
            err = chd_get_metadata(chd, CDROM_TRACK_METADATA_TAG, i, meta, sizeof(meta) - 1, &actlen, nullptr, nullptr);
            if(CHDERR_NONE != err)
            {
                break;
            }
        }

        int trackNum = 0, frames = 0, pregap = 0, postgap = 0;
        char typeStr[64] = {0}, subtypeStr[64] = {0}, pgtypeStr[64] = {0}, pgsubStr[64] = {0};

        if(sscanf(meta, CDROM_TRACK_METADATA2_FORMAT, &trackNum, typeStr, subtypeStr, &frames, &pregap, pgtypeStr, pgsubStr, &postgap) < 4)
        {
            if(sscanf(meta, CDROM_TRACK_METADATA_FORMAT, &trackNum, typeStr, subtypeStr, &frames) < 4)
            {
                if(i == 0)
                {
                    return CHDERROR_INVALID_METADATA;
                }
                break;
            }
        }

        unsigned int trackType = TRACK_UNKNOWNTYPE;
        if(0 == strcmp(typeStr, "MODE1") || 0 == strcmp(typeStr, "MODE1_RAW"))
        {
            trackType = TRACK_MODE1_DATA;
        }
        else if(0 == strcmp(typeStr, "MODE2") || 0 == strcmp(typeStr, "MODE2_RAW") || 0 == strcmp(typeStr, "MODE2_FORM1") || 0 == strcmp(typeStr, "MODE2_FORM2") || 0 == strcmp(typeStr, "MODE2_FORM_MIX"))
        {
            trackType = TRACK_MODE2_DATA;
        }
        else if(0 == strcmp(typeStr, "AUDIO"))
        {
            trackType = TRACK_AUDIO;
        }
        else
        {
            return CHDERROR_UNSUPPORTED_TRACK_TYPE;
        }

        Track trk;
        trk.trackNum = trackNum;
        trk.trackType = trackType;
        trk.sectorLength = SECTOR_DATA_SIZE;
        trk.firstLBA = currentLBA;
        trk.index01LBA = currentLBA + pregap;
        trk.dataStartLBA = trk.index01LBA;
        trk.numFramesInCHD = frames;
        trk.postGapFrames = postgap;
        trk.locationInImage = currentLocInImg + (uint64_t)pregap * SECTOR_DATA_SIZE;
        trk.chdFrame = currentCHDFrame;

        tracks.push_back(trk);

        if(pregap > 0)
        {
            Region rPregap;
            rPregap.locationInImage = currentLocInImg;
            rPregap.numBytes = (uint64_t)pregap * SECTOR_DATA_SIZE;
            rPregap.sectorLength = SECTOR_DATA_SIZE;
            rPregap.chdFrame = 0;
            rPregap.inCHD = false;
            rPregap.audio = (trackType == TRACK_AUDIO);
            regions.push_back(rPregap);

            currentLocInImg += rPregap.numBytes;
            currentLBA += pregap;
        }

        if(frames > 0)
        {
            Region rData;
            rData.locationInImage = currentLocInImg;
            rData.numBytes = (uint64_t)frames * SECTOR_DATA_SIZE;
            rData.sectorLength = SECTOR_DATA_SIZE;
            rData.chdFrame = currentCHDFrame;
            rData.inCHD = true;
            rData.audio = (trackType == TRACK_AUDIO);
            regions.push_back(rData);

            currentLocInImg += rData.numBytes;
            currentLBA += frames;
            currentCHDFrame += frames;
        }

        if(postgap > 0)
        {
            Region rPost;
            rPost.locationInImage = currentLocInImg;
            rPost.numBytes = (uint64_t)postgap * SECTOR_DATA_SIZE;
            rPost.sectorLength = SECTOR_DATA_SIZE;
            rPost.chdFrame = 0;
            rPost.inCHD = false;
            rPost.audio = (trackType == TRACK_AUDIO);
            regions.push_back(rPost);

            currentLocInImg += rPost.numBytes;
            currentLBA += postgap;
        }
    }

    if(tracks.empty())
    {
        return CHDERROR_INVALID_METADATA;
    }

    numSectors = currentLBA;
    imageSize = currentLocInImg;
    return CHDERROR_NOERROR;
}

const unsigned char *CHDImage::GetFrame(unsigned int frame) const
{
    if(nullptr == chd || 0 == framesPerHunk)
    {
        return nullptr;
    }

    unsigned int hunk = frame / framesPerHunk;
    unsigned int frameInHunk = frame % framesPerHunk;

    std::lock_guard<std::mutex> lock(readLock);
    if(hunkInBuf != hunk)
    {
        chd_error err = chd_read(chd, hunk, hunkBuf.data());
        if(CHDERR_NONE != err)
        {
            return nullptr;
        }
        hunkInBuf = hunk;
    }

    const chd_header *header = chd_get_header(chd);
    uint32_t unitbytes = header ? header->unitbytes : FRAME_SIZE;
    return hunkBuf.data() + frameInHunk * unitbytes;
}

bool CHDImage::Read(unsigned char *dst, uint64_t offset, uint64_t len) const
{
    if(!IsOpen())
    {
        return false;
    }

    while(len > 0)
    {
        uint64_t sectorIndex = offset / SECTOR_DATA_SIZE;
        uint64_t offsetInSector = offset % SECTOR_DATA_SIZE;
        uint64_t bytesToCopy = std::min<uint64_t>(len, SECTOR_DATA_SIZE - offsetInSector);

        uint64_t sectorOffsetInImage = sectorIndex * SECTOR_DATA_SIZE;
        bool foundRegion = false;

        for(auto &r : regions)
        {
            if(sectorOffsetInImage >= r.locationInImage && sectorOffsetInImage < r.locationInImage + r.numBytes)
            {
                foundRegion = true;
                if(r.inCHD)
                {
                    uint64_t sectorInRegion = (sectorOffsetInImage - r.locationInImage) / SECTOR_DATA_SIZE;
                    unsigned int frame = r.chdFrame + (unsigned int)sectorInRegion;
                    const unsigned char *frameData = GetFrame(frame);
                    if(nullptr != frameData)
                    {
                        std::memcpy(dst, frameData + offsetInSector, bytesToCopy);
                    }
                    else
                    {
                        std::memset(dst, 0, bytesToCopy);
                    }
                }
                else
                {
                    std::memset(dst, 0, bytesToCopy);
                }
                break;
            }
        }

        if(!foundRegion)
        {
            std::memset(dst, 0, bytesToCopy);
        }

        dst += bytesToCopy;
        offset += bytesToCopy;
        len -= bytesToCopy;
    }

    return true;
}
