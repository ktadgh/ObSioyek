# ObSioyek
Forking Sioyek for integration with Obsidian Vault.

## Goals:
1. Parse references with Grobid and build markdown files with backlinks 
2. Automatically add highlights and comments to markdown file for recording notes
3. Use Obsidian's graph view for a litmap of papers
4. Allow scripting to build the markdown files for a folder or list of pdfs, without manually opening the GUI


## Future Work:
1. Retrospectively add highlights/ comments to markdown files
2. Add URL handling, so that Obdidian files can link back to the correct location in Sioyek


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
curl -v -F "input=@/path/to/my/pdf.pdf" http://localhost:8070/api/processReferences
```

```bash
curl -v -F "input=@/path/to/my/pdf.pdf" http://localhost:8070/api/processHeaderDocument
```



### install QT
```bash
brew uninstall qt
python3 -m venv myenv
source myenv/bin/activate
pip install aqtinstall
mkdir ~/Qt 
cd ~/Qt
aqt install-qt mac desktop 6.8.2 clang_64 -m all
```


### Building
```bash
git submodule update --init --recursive

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
