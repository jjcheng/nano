## Compile C++ app

### Install the C/C++ Extension in vscode

### Create a tasks.json file in the .vscode folder to build your project with debug symbols, see tasks.json

### Create a launch.json file in the .vscode folder to configure VS Code’s debugger, see launch.json

### intall gdb

brew install gdb

### Build your project:

### Press Ctrl+Shift+B to run your build task.

### Start Debugging:

### Press F5 or click on the Run and Debug icon on the sidebar and select your configuration.

### install xcode-select if not installed

xcode-select --install

### install opencv if not installed

brew install opencv

### check opencv is installed

pkg-config --list-all | grep opencv

### compile the app, replace main with the file name

g++ -std=c++11 -g -o main main.cpp detect.c $(pkg-config --cflags --libs opencv4)

### run

./main
