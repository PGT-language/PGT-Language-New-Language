#pragma once
#include <QColor>

class QApplication;

namespace Theme
{
    inline const QColor editor{0x121317};
    inline const QColor window{0x17181c};
    inline const QColor panel{0x1c1e23};
    inline const QColor raised{0x24262d};
    inline const QColor hover{0x2d3038};
    inline const QColor border{0x2c2f36};
    inline const QColor currentLine{0x1a1c21};

    inline const QColor text{0xd5d8de};
    inline const QColor textDim{0x8b919c};
    inline const QColor textFaint{0x50555f};

    inline const QColor accent{0x6c8cff};
    inline const QColor selection{0x283760};
    inline const QColor error{0xff6b7a};
    inline const QColor success{0x7ee0a1};
    inline const QColor warning{0xf2c06b};

    inline const QColor synKeyword{0xc792ea};
    inline const QColor synType{0x4fd6be};
    inline const QColor synFunction{0x82aaff};
    inline const QColor synBuiltin{0xffcb6b};
    inline const QColor synExported{0x89ddff};
    inline const QColor synConstant{0xff9e64};
    inline const QColor synString{0xa5e075};
    inline const QColor synComment{0x636a78};
    inline const QColor synDirective{0xff757f};

    void apply(QApplication &app);

    inline QString hex(const QColor &c)
    {
        return c.name(QColor::HexRgb);
    }
}
