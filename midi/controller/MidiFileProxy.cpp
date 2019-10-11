
#include "MidiFile.h"
#include "MidiFileProxy.h"
#include "MidiLock.h"
#include "MidiSong.h"

//#include <direct.h>
#include <iostream>
#include <assert.h>

bool MidiFileProxy::save(MidiSongPtr song, const std::string& filePath)
{
    smf::MidiFile midiFile;
    midiFile.setTPQ(480);
    const int ppq = midiFile.getTPQ();
    assert(ppq == 480);

    MidiTrackPtr track = song->getTrack(0);   // for now we always have one track here
    for (auto it : *track) {
        MidiEventPtr evt = it.second;
        MidiNoteEventPtr note = safe_cast<MidiNoteEvent>(evt);
        if (note) {
            int startTick = int(note->startTime * ppq);
            int duration = int(note->duration * ppq);
            int key = PitchUtils::cvToSemitone(note->pitchCV);
            assert(key >= 0 && key <= 127);

            int endTick = startTick + duration;

            int outputTkNum = 0;
            int outputMidiChannel = 0;
            int velocity = 0x3f;
            printf("start tick = %d end tick = %d pitch=%d\n", startTick, endTick, key);
            midiFile.addNoteOn(outputTkNum, startTick, outputMidiChannel, key, velocity);
            midiFile.addNoteOff(outputTkNum, endTick, outputMidiChannel, key);
        }
    }
    midiFile.sortTracks();
    return midiFile.write(filePath);
    //std::cout << midiFile;
    return false;
}

MidiSongPtr MidiFileProxy::load(const std::string& filename)
{
    smf::MidiFile midiFile;


#if 0
    char buffer[2000];
    _getcwd(buffer, sizeof(buffer));
    printf("cwd = %s\n", buffer);
   printf("path = %s\n", filename.c_str());
#endif
    
    bool b = midiFile.read(filename);
    if (!b) {
        printf("open failed\n");
        return nullptr;
    }
    midiFile.makeAbsoluteTicks();
    midiFile.linkNotePairs();

    assert(midiFile.isAbsoluteTicks());

    MidiSongPtr song = std::make_shared<MidiSong>();
    MidiTrackPtr track = getFirst(song, midiFile);
    if (!track) {
        //printf("get first failed\n");
        return nullptr;
    }
    song->addTrack(0, track);
    song->assertValid();
    return song;
}

MidiTrackPtr MidiFileProxy::getFirst(MidiSongPtr song, smf::MidiFile& midiFile)
{
    MidiLocker l(song->lock);
    const double ppq = midiFile.getTicksPerQuarterNote();
   
    const int tracks = midiFile.getTrackCount();

    bool foundNotes = false;
    for (int track = 0; track < tracks; track++) {
        MidiTrackPtr newTrack = std::make_shared<MidiTrack>(song->lock);
        //printf("track = %d\n", track);
        for (int event = 0; event < midiFile[track].size(); event++) {
            smf::MidiEvent& evt = midiFile[track][event];
            if (evt.isNoteOn()) {
               // cout << midifile[track][event].getDurationInSeconds();
                const double dur = double(evt.getTickDuration()) / ppq;
                const double  start = double(evt.tick) / ppq;
                const float pitch = PitchUtils::pitchToCV(0, evt.getKeyNumber());
                //printf("found note on tick %f dur %f\n", start, dur);

                MidiNoteEventPtr note = std::make_shared<MidiNoteEvent>();
                note->startTime = float(start);
                note->duration = float(dur);
                note->pitchCV = float(pitch);

                newTrack->insertEvent(note);
                foundNotes = true;
            } else if (evt.isEndOfTrack()) {
                const double start = double(evt.tick) / ppq;
                newTrack->insertEnd(float(start));
            } else if (evt.isTrackName()) {
               // std::string name = evt.getMetaContent();
               // printf("track name is %s\n", name.c_str());
            }
        }
        if (foundNotes) {
           // newTrack->_dump();
            return newTrack;
        }
    }
    return nullptr;
}