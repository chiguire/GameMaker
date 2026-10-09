# The cases of the editors oracle (see README.md): each is a key script for one editor, run in the DOS program built from the
# source and in the port, and compared. Steps are described in keyscript.ps1; the delay (BIOS ticks) is how long the program
# gets to draw before the next key.
. (Join-Path $PSScriptRoot 'mouse.ps1')
function Get-Cases {
  $load = { param($file) @('key:Enter', "text:$file", 'key:Enter', 'delay:54') }       # a "choose ..." entry with a file prompt, then time to load
  @(
    [pscustomobject]@{ Name = 'menu-main'; Program = 'menu'; Args = 'cOoL K'; Steps = @('dump:m0', 'key:Right', 'dump:m1', 'key:Down', 'dump:m2') },
    [pscustomobject]@{ Name = 'util-main'; Program = 'utility'; Steps = @('dump:u0', 'key:Right', 'dump:u1', 'key:Right', 'dump:u2') },
    [pscustomobject]@{ Name = 'util-help'; Program = 'utility'; Steps = @('key:Left', 'key:Enter', 'delay:54', 'dump:h0', 'key:Down', 'dump:h1', 'key:Esc', 'dump:h2') },

    [pscustomobject]@{ Name = 'pal-menu'; Program = 'palchos'; Steps = @('dump:p0', 'key:Down', 'dump:p1') },
    [pscustomobject]@{ Name = 'pal-edit'; Program = 'palchos'; Steps = (& $load 'sample.pal') + @('key:Enter', 'delay:54', 'dump:e0', 'key:Right', 'key:Right', 'key:Right', 'dump:e1', 'key:Left', 'dump:e2') },
    [pscustomobject]@{ Name = 'pal-save'; Program = 'palchos'; Steps = (& $load 'sample.pal') + @('key:Down', 'key:Enter', 'text:x', 'key:Enter', 'delay:72', 'dump:s0') },

    [pscustomobject]@{ Name = 'blk-menu'; Program = 'blocedit'; Steps = @('dump:b0') },
    [pscustomobject]@{ Name = 'blk-edit'; Program = 'blocedit'; Steps = (& $load 'sample.bbl' | Select-Object -First 0) + @('key:Enter', 'key:Enter', 'delay:54', 'text:sample.bbl', 'key:Enter', 'delay:54', 'key:Enter', 'text:sample.pal', 'key:Enter', 'delay:54', 'key:Enter', 'delay:72', 'dump:e0') },
    [pscustomobject]@{ Name = 'blk-save'; Program = 'blocedit'; Steps = @('key:Enter', 'key:Enter', 'delay:54', 'text:sample.bbl', 'key:Enter', 'delay:54', 'key:Down', 'key:Down', 'key:Enter', 'text:x', 'key:Enter', 'delay:72', 'dump:s0') },

    [pscustomobject]@{ Name = 'mon-edit'; Program = 'monedit'; Steps = (& $load 'sample.mon') + @('key:Enter', 'text:sample.mbl', 'key:Enter', 'delay:54', 'key:Enter', 'text:sample.pal', 'key:Enter', 'delay:54', 'key:Enter', 'delay:72', 'dump:e0') },
    [pscustomobject]@{ Name = 'mon-save'; Program = 'monedit'; Steps = (& $load 'sample.mon') + @('key:Down', 'key:Down', 'key:Down', 'key:Enter', 'text:x', 'key:Enter', 'delay:72', 'dump:s0') },

    [pscustomobject]@{ Name = 'map-edit'; Program = 'mapmaker'; Steps = (& $load 'sample.map') + @('key:Enter', 'text:sample.bbl', 'key:Enter', 'delay:54', 'key:Enter', 'text:sample.mon', 'key:Enter', 'delay:54', 'key:Enter', 'text:sample.mbl', 'key:Enter', 'delay:54', 'key:Enter', 'text:sample.pal', 'key:Enter', 'delay:54', 'key:Enter', 'delay:90', 'dump:e0') },
    [pscustomobject]@{ Name = 'map-save'; Program = 'mapmaker'; Steps = (& $load 'sample.map') + @('key:Down', 'key:Down', 'key:Down', 'key:Down', 'key:Down', 'key:Enter', 'text:x', 'key:Enter', 'delay:72', 'dump:s0') },

    [pscustomobject]@{ Name = 'chr-edit'; Program = 'charedit'; Steps = (& $load 'sample.chr') + @('key:Enter', 'text:sample.cbl', 'key:Enter', 'delay:54', 'key:Enter', 'text:sample.pal', 'key:Enter', 'delay:54', 'key:Enter', 'text:sample.snd', 'key:Enter', 'delay:54', 'key:Enter', 'delay:90', 'dump:e0') },
    [pscustomobject]@{ Name = 'chr-save'; Program = 'charedit'; Steps = (& $load 'sample.chr') + @('key:Down', 'key:Down', 'key:Down', 'key:Down', 'key:Enter', 'text:x', 'key:Enter', 'delay:72', 'dump:s0') },

    [pscustomobject]@{ Name = 'snd-edit'; Program = 'sndedit'; Steps = (& $load 'sample.snd') + @('key:Enter', 'delay:72', 'dump:e0') },
    [pscustomobject]@{ Name = 'snd-save'; Program = 'sndedit'; Steps = (& $load 'sample.snd') + @('key:Down', 'key:Enter', 'text:x', 'key:Enter', 'delay:72', 'dump:s0') },

    [pscustomobject]@{ Name = 'gam-edit'; Program = 'grator'; Steps = (& $load 'sample.gam') + @('key:Enter', 'delay:90', 'dump:e0') },
    [pscustomobject]@{ Name = 'gam-save'; Program = 'grator'; Steps = (& $load 'sample.gam') + @('key:Down', 'key:Enter', 'text:x', 'key:Enter', 'delay:72', 'dump:s0') },

    [pscustomobject]@{ Name = 'img-menu'; Program = 'image'; Steps = @('dump:i0', 'key:Down', 'dump:i1') }
  )
}
