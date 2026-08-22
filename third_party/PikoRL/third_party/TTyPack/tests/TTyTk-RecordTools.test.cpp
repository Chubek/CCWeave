#include <TTyTk/TTyTk-RecordTools.hpp>
#include <cassert>
int main() { ttytk::Recorder recorder; recorder.start(); recorder.record_output("x"); recorder.stop(); assert(recorder.save_asciicast("/tmp/ttytk-record-test.cast")); }
