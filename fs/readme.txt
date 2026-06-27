kernel-hobby filesystem (Stage 6)

A tiny read-only filesystem on a virtio-blk device:
  block 0   superblock (magic + file count)
  block 1   directory (name, size, start block)
  block 2.. file data

Try: ls, cat motd.txt, cat readme.txt
