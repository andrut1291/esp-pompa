#include <string>

class Webpage{
    public:
    Webpage() = default;
    ~Webpage(); // Declare the destructor
    char* buildWebpage(bool isOn, float level, unsigned long currentMillis, unsigned long previousMillis, bool automatic, unsigned long goAutomaticAfter);
    private:
    std::string roundFloatToString(float val);
};