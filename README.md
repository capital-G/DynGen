# DynGen

**Dyn**amicU**Gen** evaluates [EEL2](https://www.cockos.com/EEL2/) code on the server, which allows to dynamically write  DSP code of UGens which can be updated while its running.

## Installation

Builds are occasionally uploaded at <https://github.com/capital-G/DynGen/releases>.
Download and extract the content of the archive into the `Platform.userExtensionDir` folder.

### macOS De-Quarantine

Since the plugin is not notarized it needs to be de-quarantined.
Run the following command within SuperCollider, assuming you have installed DynGen like specified above

```supercollider
"xattr -rd com.apple.quarantine \"%/DynGen\"".format(Platform.userExtensionDir).unixCmd;
```

### Build

On x86-64 machines it is necessary to have [nasm](https://www.nasm.us/) installed.

```shell
git clone --recursive https://github.com/capital-G/DynGen.git
cd DynGen

# replace SC_SRC_PATH with the path to your local source code copy of SuperCollider.
cmake \
    -S . \
    -B build \
    -DSC_SRC_PATH=/Users/scheiba/github/supercollider
cmake --build build --config Release
cmake --install build --config Release
```

By default, DynGen will be installed to the default SuperCollider extension directory.
You can override the installation path by setting the `SC_INSTALL_PATH` variable.

## Demo

Scripts are registered on the server via `DynGen` and behaves like `SynthDef`.

All inputs are available via the variables `in0`, `in1`, ... and the outputs need to be written to `out0`, `out1`, ...

### In = Output * 0.5

```supercollider
s.boot;

// registers the script on the server with identifier \simple
// like on SynthDef, remember to call .send
~simple = DynGenDef(\simple, "out0 = in0 * 0.5;").send;

// spawn a synth which evaluates our script
(
Ndef(\x, {DynGen.ar(
	1, // numOutputs
	~simple, // script to use - can also be DynGenDef(\simple) or \simple
	SinOsc.ar(200.0), // ... the inputs to the script
);
}).scope;
)
```

### Feedback SinOsc

One advantage over using plain UGens is the ability to access the prior sample.
We can use this to, e.g., write a phase modulatable `SinOscFB`

```supercollider
(
~sinOscFB = DynGenDef(\sinOscFB, "
phase += 0;
y1 += 0;
twoPi = 2*$pi;
inc += 0;

inc = twoPi * _freq / srate;

x = phase + (_fb * y1) + _phaseMod;

phase += inc;
// wrap phase
phase -= (phase >= twoPi) * twoPi;

out0 = sin(x);

y1 = out0;
").send;
)

(
Ndef(\x, {
	var sig = DynGen.ar(1, ~sinOscFB, params: [
		freq: \freq.ar(100.0),
		fb: \fb.ar(0.6, spec: [0.0, pi]),
		phaseMod: SinOsc.ar(\phaseModFreq.ar(1000.0 * pi)) * \modAmt.ar(0.0, spec:[0.0, 1000.0]),
	]);
	sig * 0.1;
}).play.gui;
)
```

#### Delay line

Every DynGen instance also has a dedicated memory region, which allows to write time-based effects.
We can use this to write e.g. a sample accurate modulatable delay line.

```supercollider
(
~delayLine = DynGenDef(\delayLine, "
buf[_writePos] = in0;
out0 = buf[_readPos];
").send;
)

(
Ndef(\x, {
	var bufSize = SinOsc.ar(4.2).range(1000, 2000);
	var writePos = LFSaw.ar(2.0, 0.02).range(1, bufSize);
	var readPos = LFSaw.ar(pi, 0.0).range(1, bufSize);
	var sig = DynGen.ar(1, ~delayLine,
		SinOsc.ar(100.0),
		params: [
		    writePos: writePos.floor,
		    readPos: readPos.floor,
        ],
	);
	sig.dup * 0.1;
}).play;
)
```

For further information and examples, look into the docs of `DynGen`. 

## License

[EEL2](https://www.cockos.com/EEL2/) and [WDL](https://www.cockos.com/wdl/) by [*Cockos*](https://www.cockos.com/) are BSD licensed.

This project is GPL-3.0 licensed.
