# Supported builds

| compact | version | status |
| ---: | --- | --- |
| 1000 | 1.0.0.0 | supported |
| 1004 | 1.0.0.4 | supported |
| 1040 | 1.0.4.0 | supported |
| 1060 | 1.0.6.0 | supported |
| 1061 | 1.0.6.1 | recognised, unverified |
| 1070 | 1.0.7.0 | supported |
| 1080 | 1.0.8.0 | supported |
| 1100 | 1.1.0.0 (EFLC) | recognised, unverified |
| 1110 | 1.1.1.0 (TLAD) | supported |
| 1120 | 1.1.2.0 (TBoGT) | supported |
| 1130 | 1.1.3.0 (EFLC) | supported |
| 12043 | 1.2.0.43 | recognised, unverified |

`1.2.0.59` and newer are deliberately **unknown**: the classifier returns
`GameBuild::unknown` and the loader installs nothing. The game's executable is a
recompile, and no per-build data exists for it.

For the unverified builds the classifier and the generic pattern path are
present, but no per-build RVA data was invented. A pattern row applies when its
signature matches; an RVA row applies only when its build gate includes the
build. This is honest about what was verified and what was not.

## How the build is read

`QueryGameVersion` reads the executable's `VS_FIXEDFILEINFO`, composes the four
fields as a decimal integer (`1.0.7.0 -> 1070`) and passes it to
`ClassifyGameBuild`, the switch the original loader uses. The enum's numeric
value is the original build id and is what the build masks index.
