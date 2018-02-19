TRANSCENDENT MEMORY BACKEND

This collection of modules is used to provide support for a subset of 
Transcendent Memory functionality over a character device. Transcendent 
Memory is a concept introduced in the Linux kernel around 2009, whose goal
is to provide a pool of not directly addressable memory; that way, since every
access to the pages in the pool require explicit calls to a backend, there
is increased flexibility in the way we store the data (e.g. remotely), as
well as in the way we represent them (e.g. the data could be compressed,
as done by the zswap module).

The modules here include the character device used to expose the tmem functionality
to userspace, the various backends that provide different kinds of functionality, as
well as modules used to connect the tmem pool with services that make use of it 
in the kernel itself (i.e. frontswap; cleancache could also be implemented).

