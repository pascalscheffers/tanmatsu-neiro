// test_midi_parse.cpp — host tests for control/midi_in.h (Stage 5a-i).
#include "control/midi_in.h"
#include "runner.h"

/* Helper: feed a sequence of bytes and return the decoded MidiMsg.
 * Asserts that exactly one message is produced within the sequence. */
static MidiMsg feed(MidiParser* p, const uint8_t* bytes, int len) {
    MidiMsg msg     = {MIDI_OTHER, 0, 0, 0};
    int     emitted = 0;
    for (int i = 0; i < len; i++) {
        if (midi_parse_byte(p, bytes[i], &msg)) {
            emitted++;
        }
    }
    TEST_ASSERT(emitted == 1, "expected exactly one message from byte sequence");
    return msg;
}

/* 1. Simple Note-On -------------------------------------------------------- */
static void test_note_on(void) {
    test_begin("Note-On 0x90 0x3C 0x64 -> MIDI_NOTE_ON ch=0 d1=60 d2=100");
    MidiParser p;
    midi_parser_init(&p);
    uint8_t bytes[] = {0x90, 0x3C, 0x64};
    MidiMsg msg     = feed(&p, bytes, 3);
    TEST_ASSERT(msg.type == MIDI_NOTE_ON, "type must be MIDI_NOTE_ON");
    TEST_ASSERT(msg.channel == 0, "channel must be 0");
    TEST_ASSERT(msg.data1 == 60, "data1 must be 60 (0x3C)");
    TEST_ASSERT(msg.data2 == 100, "data2 must be 100 (0x64)");
    test_pass();
}

/* 2. Note-Off -------------------------------------------------------------- */
static void test_note_off(void) {
    test_begin("Note-Off 0x80 0x3C 0x40 -> MIDI_NOTE_OFF");
    MidiParser p;
    midi_parser_init(&p);
    uint8_t bytes[] = {0x80, 0x3C, 0x40};
    MidiMsg msg     = feed(&p, bytes, 3);
    TEST_ASSERT(msg.type == MIDI_NOTE_OFF, "type must be MIDI_NOTE_OFF");
    TEST_ASSERT(msg.channel == 0, "channel must be 0");
    TEST_ASSERT(msg.data1 == 60, "data1 must be 60");
    test_pass();
}

/* 3. Note-On velocity 0 normalised to Note-Off ----------------------------- */
static void test_note_on_vel0(void) {
    test_begin("Note-On velocity 0 -> MIDI_NOTE_OFF");
    MidiParser p;
    midi_parser_init(&p);
    uint8_t bytes[] = {0x90, 0x3C, 0x00};
    MidiMsg msg     = feed(&p, bytes, 3);
    TEST_ASSERT(msg.type == MIDI_NOTE_OFF, "velocity-0 Note-On must be MIDI_NOTE_OFF");
    TEST_ASSERT(msg.channel == 0, "channel must be 0");
    TEST_ASSERT(msg.data1 == 60, "data1 must be 60");
    test_pass();
}

/* 4. Running status -------------------------------------------------------- */
static void test_running_status(void) {
    test_begin("Running status: 3 messages, channel preserved");
    MidiParser p;
    midi_parser_init(&p);

    /* 0x90: Note-On ch0; then three pairs of data bytes (running status) */
    /* Bytes:  status  note vel   note vel   note vel(0)                  */
    uint8_t bytes[] = {0x90, 0x3C, 0x64, 0x40, 0x64, 0x43, 0x00};

    MidiMsg msgs[3];
    int     emitted = 0;
    for (int i = 0; i < (int)(sizeof(bytes)); i++) {
        MidiMsg m;
        if (midi_parse_byte(&p, bytes[i], &m)) {
            TEST_ASSERT(emitted < 3, "too many messages emitted");
            msgs[emitted++] = m;
        }
    }
    TEST_ASSERT(emitted == 3, "expected 3 messages from running-status sequence");

    /* First: Note-On ch0 note=60 vel=100 */
    TEST_ASSERT(msgs[0].type == MIDI_NOTE_ON, "msg[0] must be Note-On");
    TEST_ASSERT(msgs[0].channel == 0, "msg[0] channel must be 0");
    TEST_ASSERT(msgs[0].data1 == 0x3C, "msg[0] data1 must be 0x3C");
    TEST_ASSERT(msgs[0].data2 == 0x64, "msg[0] data2 must be 0x64");

    /* Second: Note-On ch0 note=0x40 vel=100 */
    TEST_ASSERT(msgs[1].type == MIDI_NOTE_ON, "msg[1] must be Note-On");
    TEST_ASSERT(msgs[1].channel == 0, "msg[1] channel must be 0");
    TEST_ASSERT(msgs[1].data1 == 0x40, "msg[1] data1 must be 0x40");
    TEST_ASSERT(msgs[1].data2 == 0x64, "msg[1] data2 must be 0x64");

    /* Third: Note-On vel=0 -> Note-Off, note=0x43 */
    TEST_ASSERT(msgs[2].type == MIDI_NOTE_OFF, "msg[2] vel-0 must be Note-Off");
    TEST_ASSERT(msgs[2].channel == 0, "msg[2] channel must be 0");
    TEST_ASSERT(msgs[2].data1 == 0x43, "msg[2] data1 must be 0x43");

    test_pass();
}

/* 5. CC framing ------------------------------------------------------------ */
static void test_cc(void) {
    test_begin("CC 0xB0 0x07 0x7F -> MIDI_CC d1=7 d2=127");
    MidiParser p;
    midi_parser_init(&p);
    uint8_t bytes[] = {0xB0, 0x07, 0x7F};
    MidiMsg msg     = feed(&p, bytes, 3);
    TEST_ASSERT(msg.type == MIDI_CC, "type must be MIDI_CC");
    TEST_ASSERT(msg.channel == 0, "channel must be 0");
    TEST_ASSERT(msg.data1 == 7, "data1 must be 7 (volume CC)");
    TEST_ASSERT(msg.data2 == 127, "data2 must be 127");
    test_pass();
}

/* 6. Interleaved Real-Time byte ------------------------------------------- */
static void test_realtime_interleaved(void) {
    test_begin("RT byte 0xF8 interleaved in Note-On data -> still decodes correctly");
    MidiParser p;
    midi_parser_init(&p);

    /* 0x90 0x3C [0xF8 interleaved] 0x64 -> Note-On note=60 vel=100 */
    uint8_t bytes[] = {0x90, 0x3C, 0xF8, 0x64};
    MidiMsg msg;
    int     emitted = 0;
    for (int i = 0; i < 4; i++) {
        if (midi_parse_byte(&p, bytes[i], &msg)) {
            emitted++;
        }
    }
    TEST_ASSERT(emitted == 1, "must emit exactly one message despite RT byte");
    TEST_ASSERT(msg.type == MIDI_NOTE_ON, "type must be MIDI_NOTE_ON");
    TEST_ASSERT(msg.data1 == 60, "data1 must be 60");
    TEST_ASSERT(msg.data2 == 100, "data2 must be 100");
    test_pass();
}

/* 7. 1-data-byte message (Program Change) --------------------------------- */
static void test_program_change(void) {
    test_begin("Program Change 0xC0 0x05 -> MIDI_OTHER, does not consume next status");
    MidiParser p;
    midi_parser_init(&p);

    /* Program Change then a fresh Note-On — the Note-On status byte must NOT
     * be misread as the second data byte of the Program Change. */
    MidiMsg msgs[2];
    int     emitted = 0;

    uint8_t bytes[] = {0xC0, 0x05, 0x90, 0x3C, 0x64};
    for (int i = 0; i < (int)(sizeof(bytes)); i++) {
        MidiMsg m;
        if (midi_parse_byte(&p, bytes[i], &m)) {
            TEST_ASSERT(emitted < 2, "too many messages");
            msgs[emitted++] = m;
        }
    }

    TEST_ASSERT(emitted == 2, "expected 2 messages (Prog Change + Note-On)");

    /* First: Program Change -> MIDI_OTHER */
    TEST_ASSERT(msgs[0].type == MIDI_OTHER, "Prog Change must be MIDI_OTHER");
    TEST_ASSERT(msgs[0].channel == 0, "Prog Change channel must be 0");
    TEST_ASSERT(msgs[0].data1 == 0x05, "Prog Change data1 must be 5");

    /* Second: Note-On, not corrupted */
    TEST_ASSERT(msgs[1].type == MIDI_NOTE_ON, "Note-On after Prog Change must be MIDI_NOTE_ON");
    TEST_ASSERT(msgs[1].data1 == 0x3C, "Note-On data1 must be 0x3C");
    TEST_ASSERT(msgs[1].data2 == 0x64, "Note-On data2 must be 0x64");

    test_pass();
}

/* --------------------------------------------------------------------------
 * Suite entry point
 * -------------------------------------------------------------------------- */

void test_midi_parse_suite(void) {
    printf("--- control/midi_in (MIDI parser) ---\n");
    test_note_on();
    test_note_off();
    test_note_on_vel0();
    test_running_status();
    test_cc();
    test_realtime_interleaved();
    test_program_change();
}
