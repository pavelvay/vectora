# Vectora pacman repository

Add to `/etc/pacman.conf`:

```
[vectora]
SigLevel = Optional TrustAll
Server = https://raw.githubusercontent.com/pavelvay/vectora/pacman-repo/x86_64
```

Then:

```
sudo pacman -Sy vectora
```

Or install the package directly:

```
sudo pacman -U https://raw.githubusercontent.com/pavelvay/vectora/pacman-repo/x86_64/vectora-0.2.6-1-x86_64.pkg.tar.zst
```
