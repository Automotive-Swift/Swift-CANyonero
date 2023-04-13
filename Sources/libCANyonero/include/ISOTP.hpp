///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#include "Helpers.hpp"


namespace CANyonero {

namespace ISOTP {

class Frame {

    enum class Type {
        
    }

}





































class Transceiver {

    enum class Behavior {
        defensive,
        strict,
    };

    enum class State {
        idle,
        sending,
        receiving,
    };

    struct Action {

        enum class Type {
            process,
            writeFrames,
            waitForMore,
        };
    };





};

}
}