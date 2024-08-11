# 1장: 개발 환경의 구축

리눅스 커널 개발을 하는데에 다양한 IDE를 활용을 할 수 있습니다. 따라서, 각자 본인에게 맞는 가장 좋은 툴을 사용하면 될 것 같습니다.

하지만 우리는 가장 기초적인 개발 도구인 Vim + Cscope + Ctags 조합으로 개발을 하도록 하겠습니다.

각 단계에 앞서서 아래 명령어를 통해서 프로그램을 다운받아주도록 합니다.

```bash
sudo apt update -y
sudo apt install vim cscope universal-ctags git make gcc -y
```

그리고 linux kernel 소스 코드를 받아오도록 합니다.

```bash
git clone --depth=1 --branch=v5.15 https://github.com/torvalds/linux.git
cd linux
```

branch 매개 변수를 통해서 내가 사용하고자 하는 커널 버전을 선택해주고,
depth 매개 변수를 1로 설정해서 모든 형상을 가져오는 것을 방지합니다.

## vim

먼저 텍스트 에디터인 vim을 쉽게 쓰기 위해서 다음 명령어를 실행해서 기초 설정을 완료해줍니다.
만약에 자신만의 vim 설정 파일이 있다면 다음 부분을 무시해도 괜찮습니다.

```bash
git clone --depth=1 https://github.com/amix/vimrc.git ~/.vim_runtime
sh ~/.vim_runtime/install_awesome_vimrc.sh
```

그리고 vim을 실행해보도록 합니다.

```bash
vi sample.c
```

그러면 창이 뜨고 창에서 입력을 하기 위해서는 `i` 키를 눌러주면 됩니다.
만약에 입력을 종료하고 나가고 싶다면 `ESC` 버튼을 눌러준 후에 `:wq`를 눌러주시면 방금 작성한 내용이 `sample.c` 파일에 저장됨을 알 수 있습니다.

좀 더 자세한 vim의 사용 방법을 알고 싶다면, [링크](https://www.phys.hawaii.edu/~mza/PC/vim.html)를 확인해주시길 바랍니다.

## ctags & cscope

ctags를 사용하면 함수, 변수, 매크로가 정의된 부분을 찾아갈 수 있습니다.
이를 사용하기 위해서 다음 명령어를 방금 다운 받은 리눅스 커널 소스 코드 디렉터리에서 실행시켜줍니다.

cscope는 ctags에서 확장되어서 정의된 부분 뿐만 아니라 실제 코드가 사용되는 위치를 알 수 있습니다.
정규식을 활용해서 함수 전체 이름을 몰라도 찾을 수 있는 장점이 있습니다.

### 리눅스 커널 분석의 경우

```bash
make oldconfig # 물음이 나올텐데 특별한 설정이 없다면 엔터누르시면 됩니다.
make tags cscope
```

시간이 좀 지나서 명령어가 완료되면  `cscope.files`, `cscope.out`, `tags` 파일이 만들어지는 것을 확인할 수 있습니다.

이 파일을 vim에 불러올 수 있도록 다음 내용을 `~/.vim_runtime/my_configs.vim`에 넣어주시길 바랍니다.

```
set tags=./tags,tags;
if filereadable("./cscope.out")
    cs add cscope.out
endif
```

이 다음에 리눅스 커널 파일을 아무거나 열고 ctags, cscope 사용법에 따라서 검색을 자유롭게 수행하시면 됩니다.

자주 사용하는 명령어 리스트는 다음과 같습니다.
```bash
# ctags
<CTRL> + ] # 심볼의 정의를 찾으러 갑니다.
<CTRL> + O # 정의를 찾기 전 위치로 돌아갑니다.

# cscope
cs help # 유효한 명령어를 찾습니다.
cs find g <symbol> # symbol이 정의된 위치를 찾습니다.
cs find c <symbol> # symbol을 부르는 함수들을 찾습니다.
cs find d <symbol> # symbol에 의해서 불려지는 함수들을 찾습니다.
cs find e <text> # text 내용에 따라서 egrep으로 text를 포함하는 모든 부분을 반환합니다.
```

### 사용자 응용 프로그램의 경우

참고로 내가 만약 사용자 영역에서 개발을 하는 경우에는 cscope, ctags를 설정하는 방법이 없으므로 직접 만들어줘야 합니다.

```bash
cscope -Rb # 현재 디렉터리를 재귀적으로 탐색해서 인덱싱합니다.
ctags -R # 현재 디렉터리를 재귀적으로 탐색해서 태그를 만듭니다.
```

### 디바이스 드라이버의 경우

혹시 본인의 코드가 특정 라이브러리를 찾고하고, 내가 해당 라이브러리를 찾아야하는 경우에는 다음과 같은 명령어를 실행해서 `cscope.files`을 만들어줘도 됩니다.

```bash
rm -rf cscope.* # 기존 cscope 파일을 삭제합니다.

# 현재 디렉터리와 linux 헤더가 있는 디렉터리에서 규칙에 맞는 파일 전부를 cscope.files에 넣어줍니다.
export LINUX_HEADERS=/usr/src/linux-headers-$(uname -r | sed -En "s/\-generic//p")
find . ${LINUX_HEADERS} \( -name '*.c' -o -name '*.cpp' -o -name '*.cc' -o -name '*.h' -o -name '*.s' -o -name '*S' \) -print > cscope.files # create a cscope.files
```

그리고 완료되면 다음 명령어를 실행해주도록 합니다.

```bash
cscope -bqk
ctags -L cscope.files
```

좀 더 자세한 내용은 [링크](https://tear94fall.github.io/lecture/2020/03/03/vim-ctags-cscope-taglist.html) 참고 부탁드립니다.

## 참고 사항

### Neovim + coc + compile_commands.json

조금 고급 사용 방법으로는 neovim이라고 vim의 상위호환 프로그램을 다운 받습니다.

그리고 [coc](https://github.com/neoclide/coc.nvim), [coc-clangd](https://github.com/clangd/coc-clangd)를 설치하고 리눅스 커널에서 `make compile_commands.json`을 통해서 컴파일 정보를 만듭니다.

마지막으로 [fzf](https://github.com/junegunn/fzf.vim)(vim에도 사용가능)를 설치해서 텍스트 검색을 좀 더 빠르게 진행할 수 있습니다.

이 방식을 사용하면 좀 더 빠른 분석과 개발을 할 수 있지만 vi에 익숙하지 않은 분들께 권고하지 않으며, 무엇보다도 환경 구축에 많은 시간이 걸릴 수 있습니다.

최근에 업데이트를 하지 않아서 좀 오래된 내용을 가지고 있을 수 있지만, 혹시 설정하고자 하시는 분이 있으시다면 참고하시는 것도 좋을 것 같습니다.

[Code Snippets](https://gist.github.com/BlaCkinkGJ/18f4ceea0fd3131a07a94bfb75eaefaf)

## vscode + compile_commands.json

vscode(Visual Studio Code)를 사용해서도 개발이 가능합니다. 하지만 vscode로 개발을 하는 경우에는 VirtualBox를 활용하는 경우에는 작은 화면으로 작업을 해야할 수 있다라는 점과 호스트에서 작업하는 경우에는 호스트에서 가상 머신으로 동기화 작업을 주기적으로 시켜줘야 한다는 점이 있습니다.

또한, compile_commands.json 파일을 인식을 시키는 과정을 거쳐야 합니다.

## 리눅스 커널 분석 관련 팁

리눅스 커널을 개발하다보면 자료들이 오래되거나 코드 분석에 난항을 겪을 수 있습니다. 이 경우에는 2가지 사이트를 적절하게 활용하면 좀 더 쉬운 개발을 할 수 있습니다.

- [lwn.net](https://lwn.net): 리눅스 커널 소스에 관련된 뉴스가 많습니다. 가끔 튜토리얼도 올라옵니다.
- [bootlin Elixir](https://elixir.bootlin.com/linux/v5.15.113/source): 리눅스 커널 소스 코드가 버전별로 인덱싱이 되어있습니다. 별도 설정 없이 함수의 정의와 사용 위치를 빠르게 웹에서 찾을 수 있습니다.
- [KernelNewbies](https://kernelnewbies.org/Documents): 커널 입문자들을 위한 자료가 많이 있습니다.
