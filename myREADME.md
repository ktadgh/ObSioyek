## installing grobid
#### Installing Java
```bash
brew install openjdk@21
sudo ln -sfn /opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk /Library/Java/JavaVirtualMachines/openjdk-21.jdk
export JAVA_HOME=$(/usr/libexec/java_home -v21)
export PATH="$JAVA_HOME/bin:$PATH"
```

#### building grobid
```bash
git clone https://github.com/kermitt2/grobid.git
cd grobid
./gradlew clean install
```


#### testing it out 
```bash
./gradlew run
```
go to `http://localhost:8070` to check it's running

```bash
curl -v -F "input=@/Users/tadghk/Desktop/Reading/papers/september 25/FP8 versus INT8 for efficient deep learning inference.pdf" http://localhost:8070/api/processReferences
```

```bash
curl -v -F "input=@/Users/tadghk/Desktop/Reading/papers/september 25/FP8 versus INT8 for efficient deep learning inference.pdf" http://localhost:8070/api/processHeaderDocument
```

```bash
curl -s "https://api.semanticscholar.org/graph/v1/paper/arXiv:2106.04560?fields=title,authors,year,referenceCount,citations,references.title,references.authors,references.doi" \
  | jq
```



### Building Sioyek on mac (i had already cloned it)
```bash
git submodule update --init --recursive
chmod +x build_mac.sh
```

install gui dependencies
```bash
brew install 'qt@5' freeglut mesa harfbuzz
brew info 'qt@5'
```

add qt to path (using path from above command)
```bash
export PATH="/opt/homebrew/opt/qt@5/bin:$PATH"
```

solving some build issues
```bash
brew install gnu-sed
export PATH="/opt/homebrew/opt/gnu-sed/libexec/gnubin:$PATH"
```


and build:
```bash
MAKE_PARALLEL=8 ./build_mac.sh
mv build/sioyek.app /Applications/ # i may skip this since I already have sioyek
sudo codesign --force --sign - --deep /Applications/sioyek.app
```


trying again - development instructions:
```bash
brew uninstall qt
python3 -m venv myenv
source myenv/bin/activate
pip install aqtinstall
mkdir ~/Qt 
cd ~/Qt
aqt install-qt mac desktop 6.8.2 clang_64 -m all
export Qt6_DIR=~/Qt/6.8.2/macos/
export QT_PLUGIN_PATH=~/Qt/6.8.2/macos/plugins
export PKG_CONFIG_PATH=~/Qt/6.8.2/macos/lib/pkgconfig
export QML2_IMPORT_PATH=~/Qt/6.8.2/macos/qml
export PATH="~/Qt/6.8.2/macos/bin:$PATH"
git clone --recursive --branch development https://github.com/ahrm/sioyek
cd sioyek
chmod +x build_mac.sh
setopt PIPE_FAIL PRINT_EXIT_VALUE ERR_RETURN SOURCE_TRACE XTRACE
MAKE_PARALLEL=8 ./build_mac.sh
mv build/sioyek.app /Applications/
sudo codesign --force --sign - --deep /Applications/sioyek.app
```


```bash
export Qt6_DIR=~/Qt/6.8.2/macos/
export QT_PLUGIN_PATH=~/Qt/6.8.2/macos/plugins
export PKG_CONFIG_PATH=~/Qt/6.8.2/macos/lib/pkgconfig
export QML2_IMPORT_PATH=~/Qt/6.8.2/macos/qml
export PATH="~/Qt/6.8.2/macos/bin:$PATH"
chmod +x build_mac.sh
setopt PIPE_FAIL PRINT_EXIT_VALUE ERR_RETURN SOURCE_TRACE XTRACE
MAKE_PARALLEL=8 ./build_mac.sh
rm -rf /Applications/sioyek.app
mv build/sioyek.app /Applications/
sudo -S codesign --force --sign - --deep /Applications/sioyek.app < ~/.sioyek_pass.txt
```


running
```bash
/Applications/sioyek.app/Contents/MacOS/sioyek
```

dealing with issues (removing all old object files)
```bash
find . -name "*.o" -delete
```
