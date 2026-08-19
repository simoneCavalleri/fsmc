#compdef fsmc fsm-gen

_fsmc() {
    local -a options
    options=(
        '(-i --input)'{-i,--input}'[Input model file]:file:_files -g "*.(sysml|mmd|mermaid|puml|plantuml|xmi|xml|scxml|json|dot|gv)"'
        '(-o --output)'{-o,--output}'[Output C++ header file]:file:_files -g "*.hpp"'
        '(-n --name)'{-n,--name}'[FSM class name]:name:'
        '--namespace[C++ namespace]:namespace:'
        '--context[Context class/struct name]:context:'
        '--std[Target C++ standard]:standard:(17 20)'
        '--c++17[Target C++17 standard]'
        '--c++20[Target C++20 standard]'
        '--standalone[Generate self-contained header with embedded runtime]'
        '--modular[Generate FSM header with external runtime include]'
        '--format[Model format]:format:(auto cameo scxml json dot sysml2 plantuml mermaid)'
        '--export-runtime[Export FSM runtime library to directory]:directory:_files -/'
        '--no-thread-safe[Do not generate thread_safe_fsm wrapper]'
        '--no-stubs[Do not include default stub functors]'
        '(-h --help)'{-h,--help}'[Show help message]'
        '(-v --version)'{-v,--version}'[Show version]'
    )

    _arguments -s $options
}

_fsmc "$@"
