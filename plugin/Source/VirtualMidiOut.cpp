#include "VirtualMidiOut.h"

namespace m2m
{

VirtualMidiOut::VirtualMidiOut() : juce::Thread ("MICtoMIDI virtual MIDI out") {}

VirtualMidiOut::~VirtualMidiOut() { close(); }

bool VirtualMidiOut::open (const juce::String& portName)
{
    close();

   #if JUCE_MAC || JUCE_LINUX || JUCE_BSD || JUCE_IOS
    port = juce::MidiOutput::createNewDevice (portName);
   #endif

    if (port == nullptr)
        return false;

    name = portName;
    startThread (juce::Thread::Priority::highest);
    active = true;
    return true;
}

void VirtualMidiOut::close()
{
    active = false;
    stopThread (500);
    port.reset();
    name.clear();
}

void VirtualMidiOut::push (const MidiEvent& e) noexcept
{
    if (! active.load (std::memory_order_acquire))
        return;

    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0)
        slots[(size_t) scope.startIndex1] = e;
    else
        dropped.fetch_add (1, std::memory_order_relaxed);
}

void VirtualMidiOut::run()
{
    // Poll rather than signal: signalling a WaitableEvent from the audio
    // thread would take a lock. 1 ms polling adds at most ~1 ms of jitter.
    while (! threadShouldExit())
    {
        while (fifo.getNumReady() > 0)
        {
            MidiEvent e;
            {
                const auto scope = fifo.read (1);
                if (scope.blockSize1 == 0)
                    break;
                e = slots[(size_t) scope.startIndex1];
            }

            const auto msg = e.type == MidiEvent::Type::NoteOn
                               ? juce::MidiMessage::noteOn (e.channel, e.note, (juce::uint8) e.velocity)
                               : juce::MidiMessage::noteOff (e.channel, e.note);
            port->sendMessageNow (msg);
        }

        juce::Thread::sleep (1);
    }
}

} // namespace m2m
