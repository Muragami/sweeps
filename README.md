# wrx-engine
The **W**ren **R**eadily e**X**tensible engine. This is a cross-platform system (desktop: Windows 10/11, Linux, MacOS X)
to run WRX applications written in [Wren](https://github.com/wren-lang/wren) and running under lua (5.1+ or
[luajit](https://github.com/LuaJIT/LuaJIT) as designed). What kind of applications? Well who knows, but the provided engine 'ewrx'
here (only one possible way to get WRX moving - see lwrx below) provides a simple software 2d graphics engine. From there, it's up to you -
 and what modules you load.

# summary and limitation
**wrx-engine** is a binding for Wren to lua (5.1+ or luajit). It also supports loading Wren dynamic library modules using
the system outlined below. If you choose to use the provided engine binary to get things moving, you have access to 2d
graphics (software engine) backed by an OpenGL window, and some basic event handling.

The precarious nature of binding lua and Wren does make for some limitations. Bridging the two VMs is not very fast, you can
read best practices below to see how to approach that, and essentially the minimum data moving between them is best.

The code places a hard limit of the breadth of Wren calling lua classes, you can't have more than 32 class types, and no more
than 32 methods called for each class. While this number could be raised, it would not be efficient and would require more
busy work coding (each method call is a distinct C function, like luamethod00(), luamethod01() etc. yikes)

# lwrx (running wrx under love2d)
lwrx is WRX for love2d, it is a module that loads the wrx dynamic library and then wraps a bunch of core love2d functions.
This allows Wren app development backed by the awesome love2d.

# ewrx (running wrx under custom host)
ewrx is a simple 2d software raster engine for WRX, it is a host executable loads the wrx dynamic library.
This allows Wren app development backed by a limited native executable.