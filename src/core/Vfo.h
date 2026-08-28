#pragma once

#include <QMetaType>

enum class Vfo
{
    Main,
    Sub,
};

enum class VfoNotch
{
    Off,
    Auto,
    Manual,
};

Q_DECLARE_METATYPE(Vfo)
Q_DECLARE_METATYPE(VfoNotch)
