# Source from bash or zsh: . scripts/env.sh
# Precedence: environment > local.env > nothing (no portable defaults for machine paths).
_zk_root=$(cd "$(dirname "${BASH_SOURCE[0]:-${(%):-%x}}")/.." && pwd)
ZUNEKIT_ROOT=$_zk_root
export ZUNEKIT_ROOT
if [ -f "$_zk_root/local.env" ]; then
  while IFS= read -r _line || [ -n "$_line" ]; do
    case $_line in ''|'#'*) continue ;; esac
    _key=${_line%%=*}
    _val=${_line#*=}
    _val=${_val#\"}; _val=${_val%\"}
    eval "_cur=\${$_key-}"
    if [ -z "$_cur" ] && [ -n "$_val" ]; then
      case $_val in /*) ;; *) _val="$_zk_root/$_val" ;; esac
      export "$_key=$_val"
    fi
  done < "$_zk_root/local.env"
fi
unset _zk_root _line _key _val _cur
