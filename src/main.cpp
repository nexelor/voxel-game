#include "engine/core/Logger.hpp"
#include "game/Application.hpp"

int main() {
    // Must happen before any other Logger::Log call — opens the
    // timestamped log file under logs/ that receives every log
    // line for this run (console only ever sees Warning+).
    Logger::Init("logs");
    
    int exitCode = 0;
    {
        Application app;

        if (!app.Initialize()) {
            exitCode = -1;
        } else {
            app.Run();
            app.Shutdown();
        }
    }

    Logger::Shutdown();
    return exitCode;
}