" vim-niri-nav.vim -- Use niri's focus direction bindings to move between vim
" splits as well. Requires the accompanying helper script.
"
" Modified for niri from https://git.sr.ht/~jcc/vim-sway-nav
" Inspired by https://github.com/christoomey/vim-tmux-navigator.

let clientserver = has("nvim") || has("clientserver")
if exists("g:loaded_vim_niri_nav") || empty($NIRI_SOCKET) || !clientserver
    finish
endif
let g:loaded_vim_niri_nav = 1

function s:setup()
    " Ensure we are running a server.
    if empty(v:servername) && !has("nvim")
        call remote_startserver($'{rand()}')
    endif

    " Create a file so the helper script knows how to send a command.
    let runtime_dir = empty($XDG_RUNTIME_DIR) ? "/tmp" : $XDG_RUNTIME_DIR
    let s:servername_file = runtime_dir . "/vim-niri-nav." . getpid() . ".servername"
    let program = has("nvim") ? "nvim" : "vim"
    call writefile([program . " " . v:servername], s:servername_file)
endfunction

" Schedule setup and cleanup.
augroup vim_niri_nav
    autocmd!
    autocmd VimEnter * call s:setup()
    autocmd VimLeavePre * call delete(s:servername_file)
augroup END

" Do some shenanigans to be compatible with jobs in vim and nvim (jobs are
" used instead of system() to avoid starting a shell and running potentially
" slow shell initialization files).
if has("nvim")
    function! s:job(cmd)
        return jobstart(a:cmd)
    endfunction
else
    function! s:job(cmd)
        return job_start(a:cmd)
    endfunction
endif

" Function to be called remotely by the helper script.
"
" caller_version allows tracking behavior changes in the shell script that
" calls this funcion so that backwards compatibility can be maintained even
" when an old version of the shell script is used with a newer version of the
" vim plugin.
"
" Returns true if nav was handled within vim, false if the caller should
" perform the nav itself. (Uses a string to avoid inconsistency in how
" v:true/v:false are printed between nvim and vim.)
function VimNiriNav(dir, caller_version = 0)
    if a:caller_version < 1
        call s:show_deprecation_warning()
    endif
    " check if vim_niri_nav_workspace is set if not set it to false
    let g:vim_niri_nav_workspace = get(g:, 'vim_niri_nav_workspace', false) 
    let l:dir_flag = get({"left": "h", "down": "j", "up": "k", "right": "l"}, a:dir)
    if g:workspace = false
        " default behaviour focus-window-[up|down]
        let l:dir_comp = get({"left": "column", "down": "window", "up": "window", "right": "column"}, a:dir)
    elseif g:workspace = true
        " focus-window-or-workspace-[up|down]
        let l:dir_comp = get({"left": "column", "down": "window-or-workspace", "up": "window-or-workspace", "right": "column"}, a:dir)
    endif
    if winnr(l:dir_flag) == winnr()
        if a:caller_version < 1
            call s:job(["niri", "msg", "action", "focus-" . a:dir_comp . "-" . a:dir])
            return "true"
        else
            return "false"
        endif
    else
        execute "wincmd " . l:dir_flag
        return "true"
    endif
endfunction

let s:deprecation_warning_shown = v:false
function s:show_deprecation_warning()
    if s:deprecation_warning_shown
        return
    endif

    echohl WarningMsg
    echom "You are using a deprecated version of the vim-niri-nav shell script. Please update by downloading the latest version."
    echohl None
    let s:deprecation_warning_shown = v:true
endfunction
