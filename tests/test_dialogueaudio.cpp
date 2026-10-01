#include "dialogueaudio.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    try {
        albion::gui::DialogueAudioPlayer player;
        // Silence exercises the actual output clock without making a sound.
        player.load(std::vector<int16_t>(48000 * 2), 48000, 1);
        std::string error;
        check(player.seek(2, error), "stopped seek failed");
        check(std::abs(player.position() - 2) < .0001, "stopped seek lost the end position");
        if (!player.play(error)) {
            std::cout << "SKIP: Windows audio output unavailable: " << error << '\n';
            return 77;
        }
        check(player.playing(), "explicit Play at the end did not restart");
        check(player.position() < .1, "explicit Play did not return to the start");
        check(player.seek(2, error), "playing seek failed");
        check(!player.playing(), "seeking to the end restarted playback");
        check(std::abs(player.position() - 2) < .0001, "playing seek lost the end position");
        check(player.play(error), "replay failed");
        check(player.seek(.75, error), "mid-clip seek failed");
        check(player.playing() && player.position() >= .75 && player.position() < .85,
              "mid-clip seek did not preserve playback and position");
        player.pause();
        check(player.paused() && !player.playing(), "pause failed");
        check(player.seek(1.25, error), "paused seek failed");
        check(!player.playing() && std::abs(player.position() - 1.25) < .0001,
              "paused seek changed transport state");
        player.stop();
        check(!player.playing() && !player.paused() && player.position() == 0, "stop did not reset");
        std::cout << "Dialogue audio transport: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
