#include "Clear.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "service/Editor/Editor.h"

//
#include "display/CARDPUTER/display_CARDPUTER.h"
extern const uint8_t u8g2_font_terminus28_tf[];

//

void Clear_setup()
{
    // when entering the screen
    // clear the screen
    Menu_clear();
}

//

void Clear_render()
{
    M5Cardputer.Display.setTextSize(1.0);
static lgfx::U8g2font menuFont(u8g2_font_terminus28_tf);
    M5Cardputer.Display.setFont(&menuFont);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    
    drawWrappedText("All text will be deleted. Press [Y] to continue. Any other keys to exit", 0, 40, M5Cardputer.Display.width());
}

//

void Clear_keyboard(char key)
{
    JsonDocument &app = status();

    // delete confirmed
    if (key == 'Y' || key == 'y')
    {
        // empty the file
        Editor::getInstance().clearFile();
        
        // go back to the word processor
        app["screen"] = WORDPROCESSOR;
    }

    // go back to home menu
    //
    app["menu"]["state"] = MENU_HOME;
}
