#pragma once
#include <QColor>

class QApplication;

// The only theme of the studio: dark. Every color used anywhere in the UI comes from here.
namespace Theme
{
    // Surfaces, from the deepest to the most raised.
    inline const QColor editor{0x121317};    // code editor, output, lists
    inline const QColor window{0x17181c};    // window background
    inline const QColor panel{0x1c1e23};     // docks, toolbar, tabs
    inline const QColor raised{0x24262d};    // buttons, inputs
    inline const QColor hover{0x2d3038};
    inline const QColor border{0x2c2f36};
    inline const QColor currentLine{0x1a1c21};

    // Text.
    inline const QColor text{0xd5d8de};
    inline const QColor textDim{0x8b919c};
    inline const QColor textFaint{0x50555f};

    // Accents and states.
    inline const QColor accent{0x6c8cff};
    inline const QColor selection{0x283760};
    inline const QColor error{0xff6b7a};
    inline const QColor success{0x7ee0a1};
    inline const QColor warning{0xf2c06b};

    // Syntax.
    inline const QColor synKeyword{0xc792ea};
    inline const QColor synType{0x4fd6be};
    inline const QColor synFunction{0x82aaff};
    inline const QColor synBuiltin{0xffcb6b};
    inline const QColor synExported{0x89ddff};
    inline const QColor synConstant{0xff9e64};
    inline const QColor synString{0xa5e075};
    inline const QColor synComment{0x636a78};
    inline const QColor synDirective{0xff757f};

    // Applies the dark palette and style sheet to the whole application.
    void apply(QApplication &app);

    // "#rrggbb" for use in rich text.
    inline QString hex(const QColor &c)
    {
        return c.name(QColor::HexRgb);
    }
}
