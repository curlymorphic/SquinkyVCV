

#include "MidiEvent.h"
#include "MidiSelectionModel.h"
#include "TestAuditionHost.h"

#include "asserts.h"

static void testSelectNoteAuditions()
{
    auto a = std::make_shared<TestAuditionHost>();
    MidiSelectionModel s(a);

    assert(a->notes.empty());

    MidiNoteEventPtr n = std::make_shared<MidiNoteEvent>();
    n->pitchCV = 0;
    s.select(n);
    
    assert(!a->notes.empty());
    assertEQ(a->notes.size(), 1);
    assertEQ(a->notes[0], 0);
}

static void testNotNoteNoAudition()
{
    auto a = std::make_shared<TestAuditionHost>();
    MidiSelectionModel s(a);

    assert(a->notes.empty());

    MidiEndEventPtr n = std::make_shared<MidiEndEvent>();
   // n->pitchCV = 0;

    s.select(n);

    assert(a->notes.empty());
}

static void testSelectThreeBothAudition()
{
    auto a = std::make_shared<TestAuditionHost>();
    MidiSelectionModel s(a);

    assert(a->notes.empty());

    MidiNoteEventPtr n1 = std::make_shared<MidiNoteEvent>();
    MidiNoteEventPtr n2 = std::make_shared<MidiNoteEvent>();
    MidiNoteEventPtr n3 = std::make_shared<MidiNoteEvent>();
    n1->pitchCV = 1;
    n2->pitchCV = 2;
    n3->pitchCV = 3;
    s.select(n1);
    s.extendSelection(n2);
    s.addToSelection(n3, false);

    assertEQ(a->notes.size(), 3);
    assertEQ(a->notes[0], 1);
    assertEQ(a->notes[1], 2);
    assertEQ(a->notes[1], 3);
}

void testAudition()
{
    testSelectNoteAuditions();
    testNotNoteNoAudition();
    testSelectThreeBothAudition();
}