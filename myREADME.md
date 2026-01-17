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
