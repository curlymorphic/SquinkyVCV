
#include "IMidiPlayerHost.h"
#include "MidiLock.h"
#include "MidiPlayer2.h"
#include "MidiSong.h"
#include "MidiVoice.h"
#include "MidiVoiceAssigner.h"
#include "TestHost2.h"

#include "asserts.h"
#include <memory>
#include <vector>


const float quantInterval = .001f;      // very fine to avoid messing up old tests. 
                                        // old tests are pre-quantized playback

/**
 * Makes a one-track song.
 * Track has one quarter note at t=0, duration = eighth.
 * End event at quarter note after note
 */
MidiSongPtr makeSongOneQ(float noteTime, float endTime)
{
    const float duration = .5;
    assert(endTime >= (noteTime + duration));

    MidiSongPtr song = std::make_shared<MidiSong>();
    MidiLocker l(song->lock);
    song->createTrack(0);
    MidiTrackPtr track = song->getTrack(0);

    MidiNoteEventPtr note = std::make_shared<MidiNoteEvent>();
    note->startTime = noteTime;
    note->duration = .5;
    note->pitchCV = 2.f;
    track->insertEvent(note);
    track->insertEnd(endTime);

    MidiEventPtr p = track->begin()->second;
    assert(p->type == MidiEvent::Type::Note);

    return song;
}

/**
 * Makes a one-track song.
 * Track has one quarter note at t=0, duration = eighth.
 * End event at quarter note end.
 */
MidiSongPtr makeSongOneQ()
{
    return makeSongOneQ(0, 1);
}



using TestHost2Ptr = std::shared_ptr<TestHost2>;

static void test0()
{
    MidiVoice mv;
    MidiVoiceAssigner va(&mv, 1);
}

//************************** MidiVoice tests *********************************************

void initVoices(MidiVoice* voices, int numVoices, IMidiPlayerHost* host)
{
    for (int i = 0; i < numVoices; ++i) {
        voices[i].setHost(host);
        voices[i].setIndex(i);
        voices[i].setSampleCountForRetrigger(44);           // some plausible value
    }
}

static void testMidiVoiceDefaultState()
{
    MidiVoice mv;
    assert(mv.state() == MidiVoice::State::Idle);
}

static void assertAllButZeroAreInit(TestHost2 *th)
{
    for (int i = 1; i < 16; ++i) {
        assertLT(th->cvValue[i], -10);
        assert(!th->gateState[i]);
    }
}

static void testMidiVoicePlayNote()
{
    TestHost2 th;
    MidiVoice mv;
    mv.setHost(&th);
    mv.playNote(3.f, 0, 1.f);      // pitch 3, dur 1

    assertAllButZeroAreInit(&th);
    assert(mv.state() == MidiVoice::State::Playing);
    assert(th.cvChangeCount == 1);
    assert(th.gateChangeCount == 1);
    assert(th.cvValue[0] == 3.f);
    assert(th.gateState[0] == true);
}


static void testMidiVoicePlayNoteVoice2()
{
    TestHost2 th;
    IMidiPlayerHost* host = &th;
    MidiVoice mv[2];

    initVoices(mv, 2, host);
   
    mv[1].playNote(3.f, 0, 1.f);      // pitch 3, dur 1

    assert(mv[1].state() == MidiVoice::State::Playing);
    assert(th.cvChangeCount == 1);
    assert(th.gateChangeCount == 1);
    assert(th.cvValue[1] == 3.f);
    assert(th.gateState[1] == true);
}

static void testMidiVoicePlayNoteOnAndOff()
{
    TestHost2 th;
    IMidiPlayerHost* host = &th;
    MidiVoice mv;
    initVoices(&mv, 1, host);

    mv.playNote(3.f, 0, 1.f);      // pitch 3, dur 1
    mv.updateToMetricTime(2);   // after note is over

    assertAllButZeroAreInit(&th);
    assert(th.cvValue[0] == 3.f);
    assert(th.gateState[0] == false);
    assert(th.cvChangeCount == 1);
    assert(th.gateChangeCount == 2);
    assert(mv.state() == MidiVoice::State::Idle);
}

static void testMidiVoiceRetrigger()
{
    TestHost2 th;
    IMidiPlayerHost* host = &th;
    MidiVoice mv;
    initVoices(&mv, 1, host);

    mv.playNote(3.f, 0, 1.f);      // pitch 3, dur 1
    assert(th.gateState[0] == true);

    mv.updateToMetricTime(1.0); // note just finished
    assert(th.gateState[0] == false);

    mv.playNote(4.f, 1, 1.f);      // pitch 4, dur 1
    assert(mv.state() == MidiVoice::State::ReTriggering);
    assert(th.gateState[0] == false);
    assert(th.cvValue[0] == 3.f);
    assert(th.gateChangeCount == 2);
    assertAllButZeroAreInit(&th);
}

static void testMidiVoiceRetrigger2()
{
    TestHost2 th;
    IMidiPlayerHost* host = &th;
    MidiVoice mv;
    initVoices(&mv, 1, host);
    mv.setSampleCountForRetrigger(100);

    mv.playNote(3.f, 0, 1.f);      // pitch 3, dur 1
    assert(th.gateState[0] == true);

    mv.updateToMetricTime(1.0); // note just finished
    assert(th.gateState[0] == false);

    mv.playNote(4.f, 1, 1.f);      // pitch 4, dur 1
    assert(mv.state() == MidiVoice::State::ReTriggering);
    assert(th.gateState[0] == false);
    assert(th.cvValue[0] == 3.f);
    assert(th.gateChangeCount == 2);

    mv.updateSampleCount(99);
    assert(th.gateState[0] == false);
    assert(th.gateChangeCount == 2);

    mv.updateSampleCount(1);
    assert(th.gateState[0] == true);
    assert(th.gateChangeCount == 3);
    assert(th.cvValue[0] == 4.f);

    assertAllButZeroAreInit(&th);
}

//************************** MidiVoiceAssigner tests **********************************

static void basicTestOfVoiceAssigner()
{
    MidiVoice vx;
    MidiVoiceAssigner va(&vx, 1);
    auto p = va.getNext(0);
    assert(p);
    assert(p == &vx);
}

static void testVoiceAssign2Notes()
{
    TestHost2 th;
    IMidiPlayerHost* host = &th;
    MidiVoice mv[2];
    initVoices(mv, 2, host);

    MidiVoiceAssigner va(mv, 2);

    auto p = va.getNext(0);
    assert(p);
    assert(p == mv);
    p->playNote(3, 0, 1);

    // first is still playing, so have to get second
    p = va.getNext(0);
    assert(p);
    assert(p == mv+1);
}

static void testVoiceReAssign()
{
    MidiVoice vx;
    MidiVoiceAssigner va(&vx, 1);
    auto p = va.getNext(0);
    assert(p);
    assert(p == &vx);

    p = va.getNext(0);
    assert(p);
    assert(p == &vx);
}

static void testVoiceAssignReUse()
{
    MidiVoice vx[4];
    MidiVoiceAssigner va(vx, 4);
    TestHost2 th;
    va.setNumVoices(4);
    initVoices(vx, 4, &th);


    const float pitch1 = 0;
    const float pitch2 = 1;

    auto p = va.getNext(pitch1);
    assert(p);
    assert(p == vx);
    p->playNote(pitch1, 0, 10);         // play long note to this voice
    assert(p->state() == MidiVoice::State::Playing);

    p = va.getNext(pitch2);
    assert(p);
    assert(p == vx+1);
    p->playNote(pitch2, 0, 10);         // play long note to this voice
    assert(p->state() == MidiVoice::State::Playing);

    //now terminate the notes
    vx[0].updateToMetricTime(20);
    assert(vx[0].state() == MidiVoice::State::Idle);
    vx[1].updateToMetricTime(20);
    assert(vx[1].state() == MidiVoice::State::Idle);

    // now re-allocate pitch 2
    p = va.getNext(pitch2);
    assert(p);
    assert(p == vx+1);
}

static void testVoiceAssingOverflow()
{
    MidiVoice vx[4];
    MidiVoiceAssigner va(vx, 4);
    TestHost2 th;
    va.setNumVoices(4);
    initVoices(vx, 4, &th);


    const float pitch1 = 0;
    const float pitch2 = 1;
    const float pitch3 = 2;
    const float pitch4 = 3;
    const float pitch5 = 4;

    auto p = va.getNext(pitch1);
    assert(p);
    assert(p == vx);
    p->playNote(pitch1, 0, 10);         // play long note to this voice
    assert(p->state() == MidiVoice::State::Playing);

    p = va.getNext(pitch2);
    assert(p);
    assert(p == vx + 1);
    p->playNote(pitch2, 0, 10);         // play long note to this voice
    assert(p->state() == MidiVoice::State::Playing);

    p = va.getNext(pitch3);
    assert(p);
    assert(p == vx + 2);
    p->playNote(pitch3, 0, 10);         // play long note to this voice
    assert(p->state() == MidiVoice::State::Playing);

    p = va.getNext(pitch4);
    assert(p);
    assert(p == vx + 3);
    p->playNote(pitch4, 0, 10);         // play long note to this voice
    assert(p->state() == MidiVoice::State::Playing);

    p = va.getNext(pitch5);
    assert(p);
    assert(p == vx + 0);
    p->playNote(pitch5, 0, 10);         // play long note to this voice
    assert(p->state() == MidiVoice::State::Playing);

}

static void testVoiceAssignOverlap()
{
    MidiVoice vx[4];
    MidiVoiceAssigner va(vx, 4);
    TestHost2 th;
    va.setNumVoices(4);
    initVoices(vx, 4, &th);

    const float pitch1 = 0;
    const float pitch2 = 1;

    vx[0].updateToMetricTime(1);
    vx[1].updateToMetricTime(1);
    auto p = va.getNext(pitch1);
    assert(p);
    assert(p == vx);
    p->playNote(pitch1, 1, 3);         // play long note to this voice
    assert(p->state() == MidiVoice::State::Playing);

    vx[0].updateToMetricTime(1.5);
    vx[1].updateToMetricTime(1.5);

    p = va.getNext(pitch2);
    assert(p);
    assert(p == vx+1);
    p->playNote(pitch1, 2, 4);         // play long note to this voice
    assert(p->state() == MidiVoice::State::Playing);

    vx[0].updateToMetricTime(5);
    vx[1].updateToMetricTime(5);
    assert(vx[0].state() == MidiVoice::State::Idle);

    assert(vx[1].state() == MidiVoice::State::Idle);
}

static void testVoiceAssignOverlapMono()
{
    MidiVoice vx[4];
    MidiVoiceAssigner va(vx, 4);
    TestHost2 th;
    va.setNumVoices(1);
    initVoices(vx, 4, &th);

    const float pitch1 = 0;
    const float pitch2 = 1;

    vx[0].updateToMetricTime(1);
    vx[1].updateToMetricTime(1);
    auto p = va.getNext(pitch1);
    assert(p);
    assert(p == vx);
    p->playNote(pitch1, 1, 3);         // play long note to this voice
    assert(p->state() == MidiVoice::State::Playing);

    vx[0].updateToMetricTime(1.5);
    vx[1].updateToMetricTime(1.5);

    p = va.getNext(pitch2);
    assert(p);
    assert(p == vx);
    p->playNote(pitch1, 2, 4);         // play long note to same voice
    assert(p->state() == MidiVoice::State::Playing);

    vx[0].updateToMetricTime(5);
    vx[1].updateToMetricTime(5);
    assert(vx[0].state() == MidiVoice::State::Idle);

    assert(vx[1].state() == MidiVoice::State::Idle);
}

static void testVoiceAssignRotate()
{
    MidiVoice vx[2];
    MidiVoiceAssigner va(vx, 2);
    TestHost2 th;
    va.setNumVoices(2);
    initVoices(vx, 2, &th);

    const float pitch1 = 0;
    const float pitch2 = 1;
    const float pitch3 = 2;
   

    // play first note
    vx[0].updateToMetricTime(1);
    vx[1].updateToMetricTime(1);

    auto p = va.getNext(pitch1);
    assert(p);
    assert(p == vx);                    // pitch 1 assigned to voice 0

    p->playNote(pitch1, 1, 3);         // play long note to this voice
    assert(p->state() == MidiVoice::State::Playing);


    // let first end
    vx[0].updateToMetricTime(5);
    vx[1].updateToMetricTime(5);
    assert(p->state() == MidiVoice::State::Idle);

    // play second
    p = va.getNext(pitch2);
    assert(p);
    assert(p == vx+1);                // pitch 2 assigned to next voice
    p->playNote(pitch1, 5, 6);        // play long note to this voice
    assert(p->state() == MidiVoice::State::Playing);

     // let second end
    vx[0].updateToMetricTime(7);
    vx[1].updateToMetricTime(7);
    assert(p->state() == MidiVoice::State::Idle);

     // play third
    p = va.getNext(pitch2);
    assert(p);
    assert(p == vx);                // pitch 2 assigned to next voice (wrap
    p->playNote(pitch1, 7, 8);        // play long note to this voice
    assert(p->state() == MidiVoice::State::Playing);
}

//********************* test helper functions ************************************************

extern MidiSongPtr makeSongOneQ();


// song has an eight note starting at time 0
static std::shared_ptr<TestHost2> makeSongOneQandRun(float time)
{
    MidiSongPtr song = makeSongOneQ();
    std::shared_ptr<TestHost2> host = std::make_shared<TestHost2>();
    MidiPlayer2 pl(host, song);

    // let's make quantization very fine so these old tests don't freak out
    pl.updateToMetricTime(time, quantInterval);

    // song is only 1.0 long
    float expectedLoopStart = std::floor(time);
    assertEQ(pl.getCurrentLoopIterationStart(), expectedLoopStart);
    
    return host;
}


MidiSongPtr makeSongOverlapQ()
{
    MidiSongPtr song = std::make_shared<MidiSong>();
    MidiLocker l(song->lock);
    song->createTrack(0);
    MidiTrackPtr track = song->getTrack(0);

    //quarter note at time 1..3
    {
        MidiNoteEventPtr note = std::make_shared<MidiNoteEvent>();
        note->startTime = 1;
        note->duration = 2;
        note->pitchCV = 2.f;
        track->insertEvent(note);
    }

    // quarter 2..4
    {
        MidiNoteEventPtr note2 = std::make_shared<MidiNoteEvent>();
        note2->startTime = 2;
        note2->duration = 2;
        note2->pitchCV = 2.1f;
        track->insertEvent(note2);
    }

    track->insertEnd(20);

    return song;
}

static std::shared_ptr<TestHost2> makeSongOverlapQandRun(float time)
{
    MidiSongPtr song = makeSongOverlapQ();
    std::shared_ptr<TestHost2> host = std::make_shared<TestHost2>();
    MidiPlayer2 pl(host, song);
    pl.setNumVoices(4);

    const float quantizationInterval = .25f;        // shouldn't matter for this test...

    pl.updateToMetricTime(.5, quantizationInterval);
    assert(host->gateChangeCount == 0);
    assert(!host->gateState[0]);
    assert(!host->gateState[1]);


    pl.updateToMetricTime(1.5, quantizationInterval);
    assert(host->gateChangeCount == 1);
    assert(host->gateState[0]);
    assert(!host->gateState[1]);


    pl.updateToMetricTime(2.5, quantizationInterval);
    assert(host->gateChangeCount == 2);
    assert(host->gateState[0]);
    assert(host->gateState[1]);


    pl.updateToMetricTime(3.5, quantizationInterval);
    assert(host->gateChangeCount == 3);
    assert(!host->gateState[0]);
    assert(host->gateState[1]);

    pl.updateToMetricTime(4.5, quantizationInterval);
    assert(host->gateChangeCount == 4);


    assert(time > 4.5);
    pl.updateToMetricTime(time, quantizationInterval);

    return host;
}

static std::shared_ptr<TestHost2> makeSongTouchingQandRun(bool exactDuration, float time)
{
    assert(exactDuration);

    MidiSongPtr song = exactDuration ? MidiSong::makeTest(MidiTrack::TestContent::FourTouchingQuarters, 0) :
        MidiSong::makeTest(MidiTrack::TestContent::FourAlmostTouchingQuarters, 0);
    std::shared_ptr<TestHost2> host = std::make_shared<TestHost2>();
    MidiPlayer2 pl(host, song);
    pl.setNumVoices(4);
    pl.updateToMetricTime(time, .25f);
    return host;

}
/**
 * runs a while, generates a lock contention, runs some more
 */
static std::shared_ptr<TestHost2> makeSongOneQandRun2(float timeBeforeLock, float timeDuringLock, float timeAfterLock)
{
    MidiSongPtr song = makeSongOneQ();
    std::shared_ptr<TestHost2> host = std::make_shared<TestHost2>();
    MidiPlayer2 pl(host, song);


    pl.updateToMetricTime(timeBeforeLock, quantInterval);
    {
        MidiLocker l(song->lock);
        pl.updateToMetricTime(timeBeforeLock + timeDuringLock, quantInterval);
    }

    pl.updateToMetricTime(timeBeforeLock + timeDuringLock + timeAfterLock, quantInterval);

       // song is only 1.0 long
    float expectedLoopStart = std::floor(timeBeforeLock + timeDuringLock + timeAfterLock);
    assertEQ(pl.getCurrentLoopIterationStart(), expectedLoopStart);

    return host;
}

//***************************** MidiPlayer2 ****************************************
// test that APIs can be called
static void testMidiPlayer0()
{
    MidiSongPtr song = MidiSong::makeTest(MidiTrack::TestContent::eightQNotes, 0);
    std::shared_ptr<TestHost2> host = std::make_shared<TestHost2>();
    MidiPlayer2 pl(host, song);
    pl.updateToMetricTime(.01f, .25f);
}

// test song has an eight note starting at time 0
// just play the first note on, but not the note off
static void testMidiPlayerOneNoteOn()
{
    std::shared_ptr<TestHost2> host = makeSongOneQandRun(2 * .24f);

    assertAllButZeroAreInit(host.get());
    assertEQ(host->lockConflicts, 0);
    assertEQ(host->gateChangeCount, 1);
    assertEQ(host->gateState[0], true);
    assertEQ(host->cvChangeCount, 1);
    assertEQ(host->cvValue[0], 2);
    assertEQ(host->lockConflicts, 0);

}

// same as test1, but with a lock contention
static void testMidiPlayerOneNoteOnWithLockContention()
{
    std::shared_ptr<TestHost2> host = makeSongOneQandRun2(2 * .20f, 2 * .01f, 2 * .03f);

    assertAllButZeroAreInit(host.get());
    assertEQ(host->gateChangeCount, 1);
    assertEQ(host->gateState[0], true);
    assertEQ(host->cvChangeCount, 1);
    assertEQ(host->cvValue[0], 2);
    assertEQ(host->lockConflicts, 1);
}

// play the first note on and off
// test2
static void testMidiPlayerOneNote()
{
    // this was wall time (1/4 sec)
    std::shared_ptr<TestHost2> host = makeSongOneQandRun(.5f);

    assertAllButZeroAreInit(host.get());
    assertEQ(host->lockConflicts, 0);
    assertEQ(host->gateChangeCount, 2);
    assertEQ(host->gateState[0], false);
    assertEQ(host->cvChangeCount, 1);
    assertEQ(host->cvValue[0], 2);
}

// play the first note on and off with lock contention
static void testMidiPlayerOneNoteLockContention()
{
    std::shared_ptr<TestHost2> host = makeSongOneQandRun2(2 * .20f, 2 * .01f, 2 * .04f);

    assertAllButZeroAreInit(host.get());
    assertEQ(host->lockConflicts, 1);
    assertEQ(host->gateChangeCount, 2);
    assertEQ(host->gateState[0], false);
    assertEQ(host->cvChangeCount, 1);
    assertEQ(host->cvValue[0], 2);
}

// loop around to first note on second time
static void testMidiPlayerOneNoteLoop()
{
    std::shared_ptr<TestHost2> host = makeSongOneQandRun(2 * .51f);

    assertAllButZeroAreInit(host.get());
    assertEQ(host->gateChangeCount, 3);
    assertEQ(host->gateState[0], true);
    assertEQ(host->cvChangeCount, 1);       // only changes once because it's one note loop
    assertEQ(host->cvValue[0], 2);
}

// loop around to first note on second time
static void testMidiPlayerOneNoteLoopLockContention()
{
    std::shared_ptr<TestHost2> host = makeSongOneQandRun2(2 * .4f, 2 * .7f, 2 * .4f);

    assertAllButZeroAreInit(host.get());
    assertGE(host->gateChangeCount, 3);
    assertEQ(host->gateState[0], true);
    assertGE(host->cvChangeCount, 1);       // only changes once because it's one note loop
    assertEQ(host->cvValue[0], 2);
}

static void testMidiPlayerReset()
{
    // make empty song, player ets.
    // play it a long time
    MidiSongPtr song = MidiSong::makeTest(MidiTrack::TestContent::empty, 0);
    std::shared_ptr<TestHost2> host = std::make_shared<TestHost2>();
    MidiPlayer2 pl(host, song);
    pl.updateToMetricTime(100, quantInterval);


    assertAllButZeroAreInit(host.get());
    assertEQ(host->gateChangeCount, 0);
    assertEQ(host->gateState[0], false);
    assertEQ(host->cvChangeCount, 0);

    // Now set new real song
    MidiSongPtr newSong = makeSongOneQ();
    {
        MidiLocker l1(newSong->lock);
        MidiLocker l2(song->lock);
        pl.setSong(newSong);
    }

    // Should play just like it does in test1
    pl.updateToMetricTime(2 * .24f, quantInterval);


    assertAllButZeroAreInit(host.get());
    assertEQ(host->lockConflicts, 0);
    assertEQ(host->gateChangeCount, 1);
    assertEQ(host->gateState[0], true);
    assertEQ(host->cvChangeCount, 1);
    assertEQ(host->cvValue[0], 2);
    assertEQ(host->lockConflicts, 0);
}

#if 0   // don't support stop any more
static void testMidiPlayerStop()
{
    std::shared_ptr<TestHost2> host = makeSongOneQandRun3(1, 100);
    assertEQ(host->gateChangeCount, 0);
    assertEQ(host->cvChangeCount, 0);
}
#endif


// four voice assigner, but only two overlapping notes
static void testMidiPlayerOverlap()
{
    std::shared_ptr<TestHost2> host = makeSongOverlapQandRun(5);
    assertEQ(host->gateChangeCount, 4);
    assert(host->gateState[0] == false);
    assert(host->gateState[1] == false);
    assert(host->gateState[2] == false);
    assert(host->cvValue[0] == 2);
    assertClose(host->cvValue[1],  2.1, .001);
}

static void testMidiPlayerLoop()
{
    // make a song with one note in the second bar
    MidiSongPtr song = makeSongOneQ(4, 100);

    std::shared_ptr<TestHost2> host = std::make_shared<TestHost2>();
    MidiPlayer2 pl(host, song);

    assert(!song->getSubrangeLoop().enabled);
    SubrangeLoop l(true, 4, 8);
    song->setSubrangeLoop(l);
    assert(song->getSubrangeLoop().enabled);

    pl.updateToMetricTime(0, .5);        // send first clock, 1/8 note

    // Expect one note played on first clock, due to loop start offset
    assertEQ(1, host->gateChangeCount);
    assert(host->gateState[0]);

    // now go to near the end of the first loop. Should be nothing playing
    pl.updateToMetricTime(.9, .5);
    assertEQ(2, host->gateChangeCount);
    assert(!host->gateState[0]);
}

static void testMidiPlayerLoop2()
{
    // make a song with one note in the second bar
    MidiSongPtr song = makeSongOneQ(4, 100);

    std::shared_ptr<TestHost2> host = std::make_shared<TestHost2>();
    MidiPlayer2 pl(host, song);

    assert(!song->getSubrangeLoop().enabled);
    SubrangeLoop l(true, 4, 8);
    song->setSubrangeLoop(l);
    assert(song->getSubrangeLoop().enabled);

    assertEQ(pl.getCurrentLoopIterationStart(), 0);
    pl.updateToMetricTime(0, .5);        // send first clock, 1/8 note

    // Expect one note played on first clock, due to loop start offset
    assertEQ(1, host->gateChangeCount);
    assert(host->gateState[0]);

    // now go to near the end of the first loop. Should be nothing playing
    pl.updateToMetricTime(3.5, .5);   
    assertEQ(2, host->gateChangeCount);
    assert(!host->gateState[0]);
    assertEQ(pl.getCurrentLoopIterationStart(), 0);

   // now go to the second time around the loop, should play again.
    pl.updateToMetricTime(4, .5);
    assert(host->gateState[0]);
    assertEQ(3, host->gateChangeCount);
    assertEQ(pl.getCurrentLoopIterationStart(), 4);
}


static void testMidiPlayerLoop3()
{
    // make a song with one note in the second bar
    MidiSongPtr song = makeSongOneQ(4, 100);

    std::shared_ptr<TestHost2> host = std::make_shared<TestHost2>();
    MidiPlayer2 pl(host, song);

    assert(!song->getSubrangeLoop().enabled);
    SubrangeLoop l(true, 4, 8);
    song->setSubrangeLoop(l);
    assert(song->getSubrangeLoop().enabled);

    pl.updateToMetricTime(0, .5);        // send first clock, 1/8 note

    // Expect one note played on first clock, due to loop start offset
    assertEQ(1, host->gateChangeCount);
    assert(host->gateState[0]);

    // now go to near the end of the first loop. Should be nothing playing
    pl.updateToMetricTime(3.5, .5);
    assertEQ(2, host->gateChangeCount);
    assert(!host->gateState[0]);

    // now go to the second time around the loop, should play again.
    pl.updateToMetricTime(4, .5);
    assert(host->gateState[0]);
    assertEQ(3, host->gateChangeCount);
    assertEQ(pl.getCurrentLoopIterationStart(), 4);

    // now go to near the end of the first loop. Should be nothing playing
    pl.updateToMetricTime(4 + 3.5, .5);
    assert(!host->gateState[0]);
    assertEQ(4, host->gateChangeCount);
    assertEQ(pl.getCurrentLoopIterationStart(), 4);

    // now go to the third time around the loop, should play again.
    pl.updateToMetricTime(4+4, .5);
    assertEQ(pl.getCurrentLoopIterationStart(), 8);
    assert(host->gateState[0]);
    assertEQ(5, host->gateChangeCount);
}

//*******************************tests of MidiPlayer2 **************************************
void testMidiPlayer2()
{
    test0();
    testMidiVoiceDefaultState();
    testMidiVoicePlayNote();
    testMidiVoicePlayNoteVoice2();
    testMidiVoicePlayNoteOnAndOff();
    testMidiVoiceRetrigger();
    testMidiVoiceRetrigger2();

    basicTestOfVoiceAssigner();
    testVoiceAssign2Notes();
    testVoiceReAssign();
    testVoiceAssignReUse();
    testVoiceAssingOverflow();
    testVoiceAssignOverlap();
    testVoiceAssignOverlapMono();
    testVoiceAssignRotate();

    testMidiPlayer0();
    testMidiPlayerOneNoteOn();
    testMidiPlayerOneNoteOnWithLockContention();
    testMidiPlayerOneNote();
    testMidiPlayerOneNoteLockContention();
    testMidiPlayerOneNoteLoop();
    testMidiPlayerOneNoteLoopLockContention();
    testMidiPlayerReset();
    testMidiPlayerOverlap();
    testMidiPlayerLoop();
    testMidiPlayerLoop2();
    testMidiPlayerLoop3();
}
