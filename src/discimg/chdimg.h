#ifndef CHDIMG_IS_INCLUDED
#define CHDIMG_IS_INCLUDED

#include <vector>
#include <string>
#include <cstdint>
#include <mutex>

class CHDImage
{
public:
    enum { FRAME_SIZE=2448, SECTOR_DATA_SIZE=2352, TRACK_PADDING=4 };
    enum { TRACK_UNKNOWNTYPE, TRACK_MODE1_DATA, TRACK_MODE2_DATA, TRACK_AUDIO };
    enum {
        CHDERROR_NOERROR,
        CHDERROR_CANNOT_OPEN,
        CHDERROR_NOT_A_CD,
        CHDERROR_INVALID_METADATA,
        CHDERROR_UNSUPPORTED_TRACK_TYPE,
    };

    class Track
    {
    public:
        unsigned int trackNum=0;
        unsigned int trackType=TRACK_UNKNOWNTYPE;
        unsigned int sectorLength=SECTOR_DATA_SIZE;
        unsigned int firstLBA=0;
        unsigned int index01LBA=0;
        unsigned int dataStartLBA=0;
        unsigned int numFramesInCHD=0;
        unsigned int postGapFrames=0;
        uint64_t locationInImage=0;
        unsigned int chdFrame=0;
    };

    CHDImage();
    ~CHDImage();
    CHDImage(const CHDImage &)=delete;
    CHDImage &operator=(const CHDImage &)=delete;

    unsigned int Open(const std::string &fName);
    void Close(void);
    bool IsOpen(void) const;

    inline const std::vector<Track> &GetTracks(void) const { return tracks; }
    inline unsigned int GetNumSectors(void) const { return numSectors; }
    inline uint64_t GetImageSize(void) const { return imageSize; }

    bool Read(unsigned char *dst, uint64_t offset, uint64_t len) const;

private:
    class Region
    {
    public:
        uint64_t locationInImage=0;
        uint64_t numBytes=0;
        unsigned int sectorLength=0;
        unsigned int chdFrame=0;
        bool inCHD=false;
        bool audio=false;
    };

    unsigned int MakeTrackTable(void);
    const unsigned char *GetFrame(unsigned int frame) const;

    struct _chd_file *chd=nullptr;
    std::vector<Track> tracks;
    std::vector<Region> regions;
    unsigned int numSectors=0;
    uint64_t imageSize=0;
    unsigned int framesPerHunk=0;

    mutable std::mutex readLock;
    mutable std::vector<unsigned char> hunkBuf;
    mutable unsigned int hunkInBuf=~0;
};

#endif
