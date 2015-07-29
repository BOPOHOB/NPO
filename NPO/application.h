#ifndef APPLICATION_H
#define APPLICATION_H

#include <QApplication>

class Project;
class Identity;
class MainWindow;

class Application : public QApplication
{
    Identity* self;
    Project* pro;
    MainWindow* window;

public:
    Application(int argc, char** argv);
    ~Application();

    void setMainWindow(MainWindow* w) { window = w; }
    static MainWindow* mainWindow();
    static const Identity* identity();
    static Project *project();
};

#endif // APPLICATION_H
