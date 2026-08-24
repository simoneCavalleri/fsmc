# bash completion for fsmc

_fsmc_completions() {
    local cur prev opts
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    opts="-i --input -o --output -t --target -n --name --namespace --package --context --std --c++17 --c++20 --standalone --modular --format -e --export --verify --export-runtime --no-thread-safe --no-stubs -h --help -v --version"

    case "${prev}" in
        -i|--input)
            COMPREPLY=( $(compgen -f -X '!*@(.sysml|.mmd|.mermaid|.puml|.plantuml|.xmi|.xml|.scxml|.json|.dot|.gv)' -- "${cur}") )
            return 0
            ;;
        -o|--output)
            COMPREPLY=( $(compgen -f -X '!*.hpp' -- "${cur}") )
            return 0
            ;;
        -t|--target)
            COMPREPLY=( $(compgen -W "cpp c rust zig ts python" -- "${cur}") )
            return 0
            ;;
        -e|--export)
            COMPREPLY=( $(compgen -W "mermaid plantuml sysml2 json" -- "${cur}") )
            return 0
            ;;
        --format)
            COMPREPLY=( $(compgen -W "auto cameo scxml json dot sysml2 plantuml mermaid" -- "${cur}") )
            return 0
            ;;
        --std)
            COMPREPLY=( $(compgen -W "17 20" -- "${cur}") )
            return 0
            ;;
        --export-runtime)
            COMPREPLY=( $(compgen -d -- "${cur}") )
            return 0
            ;;
        *)
            ;;
    esac

    if [[ "${cur}" == -* ]]; then
        COMPREPLY=( $(compgen -W "${opts}" -- "${cur}") )
        return 0
    fi
}

complete -F _fsmc_completions fsmc
