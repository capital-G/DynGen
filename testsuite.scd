(
~dynGenTests = (
	allTests: [
		\testBasic,
		\testCodeSections,
		\testDuplicateSections,
		\testCodeBeforeSections,
		\testSync,
		\testUpdate,
		\testNonExisting,
		\testCodeAfterUGen,
		\testDynamicIO,
		\testClearOutVariables,
		\testParam,
		\testMultiParam,
		\testParamSpecs,
		\testWrongParamSpecs,
		\testLinParam,
		\testStepParam,
		\testTrigParam,
		\testConstParam,
		\testInitRateParams,
		\testBufRead,
		\testBufWrite,
		\testBufInfo,
		\testBufInfoOutOfRange,
		\testSetDone,
		\testDoneAction,
		\testVars,
		\testConstants,
		\testDelete,
		\testDeleteWhileRunning,
		\testDeleteAll,
		\testInputInitSection,
		\testInputBlockSection,
		\testParamsInitSection,
		\testParamsBlockSection,

		// signal function tests
		\testClip,
		\testWrap,
		\testFold,
		\testMod,
		\testLin,
		\testCubic,

		// state function tests
		\testWrongDelta,
		\testWrongDeltaInput,
		\testWrongDeltaOutput,
		\testDelta,
		\testHistory,
		\testLatch,

		// sclang tests
		\testRemoveComments,
		\testExtractParameters,
	],

	testBasic: {
		var success = false;
		var condition = Condition();
		DynGenDef(\testBasic, "out0=0.5;").send;
		s.sync;
		{
			DynGen.ar(1, \testBasic);
		}.loadToFloatArray(0.2, action: {|sig|
			success = sig.any({|x| (x-0.5).abs <= 0.01});
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testCodeSections: {
		var success = false;
		var condition = Condition();
		// ignore unknown @ directives!
		// also test function definitions across code sections!
		DynGenDef(\testCodeSections, "
            // @foo should be ignored
            @foo
            // @bar should also be ignored
            @bar

            @param foo: 0.5
            @param bar: 1.0

            @init
            x = 0.5;
            function increment(i)
            (
                i + 1;
            );

            // add some trailing whitespace
            @sample   \n
            z = increment(z);
            out0 = x;
            out1 = y;
            out2 = z;

            // make sure that sections can be defined in any order
            @block
            y = increment(y);"
		).send;
		s.sync;
		{
			DynGen.ar(3, \testCodeSections, sync: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			var blockSize = s.options.blockSize;
			success = true;
			sig.clump(3).do { |array, i|
				var x = array[0];
				var y = array[1];
				var z = array[2];
				if (x != 0.5) { success = false };
				if (y != (i.div(blockSize) + 1)) { success = false };
				if (z != (i + 1)) { success = false };
			};
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testDuplicateSections: {
		var func = { |code|
			var success = false;
			var condition = Condition();
			DynGenDef(\testDuplicateSections, code).send;
			s.sync;
			{
				DynGen.ar(1, \testDuplicateSections, sync: 1.0);
			}.loadToFloatArray(0.01, action: {|sig|
				success = sig.every(_ == 0.0);
				condition.unhang;
			});
			condition.hang;
			success;
		};
		func.("@sample\n out0 = 1.0;\n @sample\n out0 = -1.0;") and:
		func.("@sample\n out0 = 1.0;\n @init\n x = 0.0\n @sample\n out0 = -1.0;") and:
		func.("@init\n x = 1.0;\n @init\n x = -1.0;\n @sample\n out0 = 1.0;") and:
		func.("@init\n x = 1.0;\n @sample\n out0 = -1.0;\n @init\n x = 1.0;") and:
		func.("@block\n x = 1.0;\n @block\n x = -1.0;\n @sample\n out0 = 1.0;") and:
		func.("@block\n x = 1.0;\n @sample\n out0 = -1.0;\n @block\n x = 1.0; ")
	},

	testCodeBeforeSections: {
		// currently, any code before the first section is silently ignored.
		// TODO: make this a warning or error
		var success = false;
		var condition = Condition();
		DynGenDef(\testCodeBeforeSections, "
            out0 = 0.5;

            @sample
            out0 = -0.5;"
		).send;
		s.sync;
		{
			DynGen.ar(1, \testCodeBeforeSections, sync: 1.0);
		}.loadToFloatArray(0.01, action: {|sig|
			success = sig.every(_ == -0.5);
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testSync: {
		var success = false;
		var condition = Condition();
		DynGenDef(\testSync, "out0=0.5;").send;
		s.sync;
		{
			DynGen.ar(1, \testSync, sync: 1.0);
		}.loadToFloatArray(0.2, action: {|sig|
			success = (sig[0] - 0.5).abs < 0.01;
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testUpdate: {
		var success = false;
		var condition = Condition();
		var checkUpdate = { |shouldUpdate|
			var wasUpdated = false;
			DynGenDef(\testUpdate, "out0=0.5;").send;
			s.sync;
			fork{
				0.1.wait;
				DynGenDef(\testUpdate, "out0=$pi;").send;
			};
			{
				DynGen.ar(1, \testUpdate, update: shouldUpdate);
			}.loadToFloatArray(0.4, action: {|sig|
				wasUpdated = sig.any({|x| (x-pi).abs < 0.01});
				condition.unhang;
			});
			condition.hang;
			wasUpdated;
		};
		success = checkUpdate.(1.0) and: { checkUpdate.(0.0).not };
		success;
	},

	testNonExisting: {
		// a DynGen without code should just produce silence
		var success = false;
		var condition = Condition();
		{
			DynGen.ar(1, \nonExisting);
		}.loadToFloatArray(0.01, action: {|sig|
			success = sig.every(_ == 0.0);
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testCodeAfterUGen: {
		// Send the code after creating the DynGen.
		// Despite the error message in the console, we should still be able to send the code and update the UGen.
		// Make sure this also works with parameters and with update=0.
		var success = false;
		var condition = Condition();
		// first remove the code from the Server!
		// only do is if we have code to avoid error messages.
		var def = DynGenDef(\testCodeAfterUGen);
		if (def.code.size > 0) { "free def".postln; def.free };
		// then create the UGen (without code)
		{
			DynGen.ar(2, \testCodeAfterUGen, params: [ foo: 2.0 ], update: 0.0, sync: 1.0);
		}.loadToFloatArray(0.2, action: {|sig|
			// the first two samples should be still 0.0,
			// the last two samples should match the code.
			var initSuccess = sig[0] == 0.0 and: { sig[1] == 0.0 };
			var codeSuccess = sig.wrapAt(-2) == 1.0 and: { sig.wrapAt(-1) == 2.0 };
			success = initSuccess and: codeSuccess;
			condition.unhang;
		});
		// finally send code (after some delay!)
		0.1.wait;
		DynGenDef(\testCodeAfterUGen, "out0=1.0; out1=_foo").send;
		s.sync;
		condition.hang;
		success;
	},

	testDynamicIO: {
		var success = false;
		var condition = Condition();
		DynGenDef(\testDynamicIO, "out(0)=0.5;out(1)=in(0);").send;
		s.sync;
		{
			DynGen.ar(2, \testDynamicIO, DC.ar(20.0), sync: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			var left = (sig[0]-0.5).abs < 0.01;
			var right = (sig[1]-20.0).abs < 0.01;
			success = left.and(right);
			condition.unhang;
		});
		condition.hang;
		success;
	},

	\testClearOutVariables: {
		var success = false;
		var condition = Condition();
		// set channel count from 4 to 0 after 256 samples
		DynGenDef(\testClearOutVariables, "
            n = elapsed < 256 ? 4 : 0;
            i = 0;
            loop(n, out(i) = i + 1; i += 1);
            elapsed += 1;"
		).send;
		s.sync;
		{
			DynGen.ar(4, \testClearOutVariables, sync: 1.0);
		}.loadToFloatArray(0.01, action: {|sig|
			success = sig.clump(4).every { |array, i|
				if (i < 256) {
					array.every { |out, chan|
						out == (chan + 1);
					}
				} {
					array.every(_ == 0.0);
				}
			};
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testParam: {
		var success = false;
		var condition = Condition();
		DynGenDef(\testParam, "out0=_foo;").send;
		s.sync;
		{
			DynGen.ar(1, \testParam, params: [foo: DC.ar(pi)]);
		}.loadToFloatArray(0.2, action: {|sig|
			success = sig.any({|x| (x - pi).abs < 0.01});
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testMultiParam: {
		var success = false;
		var condition = Condition();
		DynGenDef(\testMultiParam, "out0=_foo; out1 = _bar;").send;
		s.sync;
		{
			DynGen.ar(2, \testMultiParam, params: [foo: DC.ar(pi), bar: DC.ar(2)]);
		}.loadToFloatArray(0.2, action: {|sig|
			var left = (sig[1024] - pi).abs < 0.01;
			var right = (sig[1025] - 2.0).abs < 0.01;
			success = left.and(right);
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testParamSpecs: {
		var success = false;
		var condition = Condition();
		DynGenDef(\testParamSpecs, "
            // comments are ok
            @param a: 0.5
            // test placeholder
            @param b: 0.5, _, -1, 1
            // test keyword args in arbitrary order
            @param c: type=lin, max=1, min=-1, init=0.5
            // can use keyword args after positional args
            @param d: 0.5, type=lin
            // whitespace around commas is ok
            @param e: 0.5 , lin , 0 , 1
            // whitespace around = is ok
            @param f: init = 0.5, type = lin, min = 0, max = 1
            // without any whitespace
            @param g:0.5,lin,min=-1,max=1
            // trailing commas are ignored
            @param h: 0.5, lin,

            out0 = _a;
            out1 = _b;
            out2 = _c;
            out3 = _d;"
		).send;
		s.sync;
		{
			var noise = WhiteNoise.ar * 10;
			DynGen.ar(4, \testParamSpecs, params: [
				// do not modulate 'a' to check init value behavior
				// check range clipping for 'b' (ar) and 'c' (kr)
				b: noise, c: A2K.kr(noise),
				// make sure that 'd' is not clipped
				d: noise
			], sync: 1.0) ++ noise;
		}.loadToFloatArray(0.1, action: {|sig|
			success = sig.clump(5).every { |array|
				var a = array[0] == 0.5;
				var b = array[1] >= -1 and: { array[1] <= 1 };
				var c = array[2] >= -1 and: { array[2] <= 1 };
				var d = array[3] == array[4];
				a && b && c && d;
			};
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testWrongParamSpecs: {
		var condition = Condition();
		var test = { |code|
			var success = false;
			// this code should not compile, but it should not crash the Server
			DynGenDef(\testWrongParamSpecs, code).send;
			s.sync;
			{
				DynGen.ar(1, \testWrongParamSpecs, sync: 1.0);
			}.loadToFloatArray(0.01, action: {|sig|
				// the script should not compile so the output should be all zeros.
				success = sig.every(_ == 0.0);
				condition.unhang;
			});
			condition.hang;
			success;
		};
		// bad init value
		test.("@param a: foo\n out0 = _a;") and:
		// bad type
		test.("@param a: type=foo\n out0 = _a;") and:
		// bad min. value
		test.("@param a: min=foo \n out0 = _a;") and:
		// bad max. value
		test.("@param a: max=foo \n out0 = _a;") and:
		// bad keyword
		test.("@param a: foo=bar \n out0 = _a;") and:
		// parameter name not alphanumeric
		test.("@param foo!bar: 0.5 \n out0 = _a;")
	},

	testLinParam: {
		// control-rate "lin" parameters should yield the same result as K2A (except for the very first block).
		// Test with and without @init resp. @block sections!
		var func = { |code|
			var success = false;
			var condition = Condition();
			DynGenDef(\testLinParam, code).send;
			s.sync;
			{
				var noiseKr = WhiteNoise.kr;
				var noiseAr = K2A.ar(noiseKr);
				var first = Latch.ar(noiseKr, 1);
				DynGen.ar(2, \testLinParam, params: [ a: noiseAr, b: noiseKr ], sync: 1.0) ++ noiseAr ++ first;
			}.loadToFloatArray(0.02, action: {|sig|
				var maxErr = 0.0001;
				var blockSize = s.options.blockSize;
				success = sig.clump(4).every { |array, i|
					var err1, err2, a, b, noise, first;
					#a, b, noise, first = array;
					// DynGen does not have access to parameter values before the first process() call.
					// For this reason, control-rate "lin" parameters already start with the new value in the very first block.
					if (i.div(blockSize) == 0) {
						(abs(b - first) < maxErr)
					} {
						abs(a - b) < maxErr and: { (abs(b - noise) < maxErr) }
					}
				};
				condition.unhang;
			});
			condition.hang;
			success;
		};
		var result1 = func.value("out0 = _a; out1 = _b;");
		var result2 = func.value("
            @init
            x = 0;
            @sample
            out0 = _a; out1 = _b;"
		);
		var result3 = func.value("
            @block
            x = 0;
            @sample
            out0 = _a; out1 = _b;"
		);
		result1.and(result2).and(result3);
	},

	testStepParam: {
		// a control-rate "step" parameter must not be interpolated!
		// also test that audio-rate "step" parameters are passed as is.
		var success = false;
		var condition = Condition();
		DynGenDef(\testStepParam, "
            @param a: 0, step
            @param b: 0, step

            out0 = _a;
            out1 = _b;"
		).send;
		s.sync;
		{
			var noiseAr = WhiteNoise.ar;
			var noiseKr = A2K.kr(noiseAr);
			var noiseStep = Latch.ar(noiseAr, Impulse.ar(ControlRate.ir));
			DynGen.ar(2, \testStepParam, params: [a: noiseAr, b: noiseKr], sync: 1.0) ++ noiseAr ++ noiseStep;
		}.loadToFloatArray(0.1, action: {|sig|
			var maxErr = 0.0001;
			success = sig.clump(4).every { |array|
				var a, b, noise, step;
				#a, b, noise, step = array;
				abs(a - noise) < maxErr and: { abs(b - step) < maxErr };
			};
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testTrigParam: {
		// trigger inputs are converted to impulse signals.
		// If a trigger input is not modulated/set and its init value is positive,
		// it must trigger exactly once on the very first sample.
		var success = false;
		var condition = Condition();
		DynGenDef(\testTrigParam, "
            // modulated/set
            @param a: 0, trig
            @param b: 0, trig
            @param c: 0, trig
            // not modulated/set
            @param d: 1, trig
            @param e: 0, trig

            out0 = _a;
            out1 = _b;
            out2 = _c;
            out3 = _d;
            out4 = _e;"
		).send;
		s.sync;
		{
			// Use a "real" audio signal because it helps to discover bugs!
			var sigAr = SinOsc.ar(200);
			var sigKr = SinOsc.kr(200);
			var trigAr = Trig1.ar(sigAr, 0);
			var trigKr = Trig1.kr(sigKr, 0);
			DynGen.ar(5, \testTrigParam, params: [a: sigAr, b: sigKr, c: 1], sync: 1.0) ++ trigAr ++ Trig1.ar(trigKr, 0);
		}.loadToFloatArray(0.1, action: {|sig|
			var maxErr = 0.0001;
			success = sig.clump(7).every { |array, i|
				var a, b, c, d, e, trigAr, trigKr;
				#a, b, c, d, e, trigAr, trigKr = array;
				(a == trigAr) and: { b == trigKr }
				and: { c == (i == 0).asInteger }
				and: { d == c } and: { e == 0 }
			};
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testConstParam: {
		// audio-rate and control-rate "const" parameters cannot be modulated after the first sample.
		var success = false;
		var condition = Condition();
		DynGenDef(\testConstParam, "
            @param a: 0, const
            @param b: 0, const

            out0 = _a;
            out1 = _b;"
		).send;
		s.sync;
		{
			var noiseAr = WhiteNoise.ar;
			var noiseKr = A2K.kr(noiseAr);
			var noiseFirst = Latch.ar(noiseAr, 1);
			DynGen.ar(2, \testConstParam, params: [a: noiseAr, b: noiseKr], sync: 1.0) ++ noiseFirst;
		}.loadToFloatArray(0.1, action: {|sig|
			var maxErr = 0.0001;
			success = sig.clump(3).every { |array|
				var a = array[0], b = array[1], first = array[2];
				abs(a - first) < maxErr and: { abs(b - first) < maxErr };
			};
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testInitRateParams: {
		// init rate parameters must generate constant output, except for "trig" parameters.
		var success = false;
		var condition = Condition();
		var value = 0.5;
		DynGenDef(\testInitRateParams, "
            @param a: 0, lin
            @param b: 0, step
            @param c: 0, const
            @param d: 0, trig

            out0 = _a;
            out1 = _b;
            out2 = _c;
            out3 = _d;"
		).send;
		s.sync;
		{
			DynGen.ar(4, \testInitRateParams, params: [a: value, b: value, c: value, d: 1.0], sync: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			success = sig.clump(4).every { |array, i|
				var a, b, c, d;
				#a, b, c, d = array;
				(a == value) and: { b == value } and: { c == value }
				and: { d == (i == 0).asInteger }
			};
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testBufRead: {
		var success = false;
		var condition = Condition();
		var buffer = Buffer.loadCollection(s, FloatArray.fill(100, pi));
		DynGenDef(\testBufRead, "out0=bufRead(_bufnum, 0)").send;
		s.sync;
		{
			DynGen.ar(1, \testBufRead, params: [bufnum: buffer]);
		}.loadToFloatArray(0.2, action: {|sig|
			success = sig.any({|x| (x-pi).abs < 0.01;});
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testBufWrite: {
		var success = false;
		var condition = Condition();
		var bufCondition = Condition();
		var buffer = Buffer.loadCollection(s, FloatArray.fill(100, pi));

		DynGenDef(\testBufWrite, "out0=bufWrite(_bufnum, 0, 0.5)").send;
		s.sync;
		{
			DynGen.ar(1, \testBufWrite, params: [bufnum: buffer]);
		}.loadToFloatArray(0.2, action: {
			condition.unhang;
		});
		condition.hang;

		buffer.get(0, {|x|
			success = (x-0.5).abs < 0.01;
			bufCondition.unhang;
		});
		bufCondition.hang;

		success;
	},

	testBufInfo: {
		var success = false;
		var condition = Condition();
		var buffer = Buffer.read(s, ExampleFiles.apollo11);

		DynGenDef(\testBufInfo, "
            out0=bufSampleRate(_bufnum);
            out1=bufChannels(_bufnum);
            out2=bufFrames(_bufnum);"
		).send;
		s.sync;
		{
			DynGen.ar(3, \testBufInfo, params: [bufnum: buffer], sync: 1.0);
		}.loadToFloatArray(0.01, action: { |sig|
			var sr = sig[0] == buffer.sampleRate;
			var chans = sig[1] == buffer.numChannels;
			var frames = sig[2] == buffer.numFrames;
			success = sr.and(chans).and(frames);
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testBufInfoOutOfRange: {
		var success = false;
		var condition = Condition();
		var bufnum = s.options.numBuffers; // out of range
		DynGenDef(\testBufInfoOutOfRange, "
            out0=bufSampleRate(_bufnum);
            out1=bufChannels(_bufnum);
            out2=bufFrames(_bufnum);"
		).send;
		s.sync;
		{
			DynGen.ar(3, \testBufInfoOutOfRange, params: [bufnum: bufnum], sync: 1.0);
		}.loadToFloatArray(0.01, action: { |sig|
			success = sig[0..2].every(_ == 0.0);
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testSetDone: {
		var success = false;
		var condition = Condition();
		// set the 'done' flag after 256 samples
		DynGenDef(\testSetDone, "
            out0 = 0;
            elapsed += 1;
            elapsed == 256 ? setDone(1);"
		).send;
		s.sync;
		{
			var sig = DynGen.ar(1, \testSetDone, sync: 1.0);
			var done = Done.kr(sig);
			K2A.ar(done).floor;
		}.loadToFloatArray(0.01, action: { |sig|
			success = sig.every { |out, i|
				if (i < 256) { out == 0 } { out == 1 }
			};
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testDoneAction: {
		var success = false;
		var condition = Condition();
		// perform done action (2 = free self) after 256 samples
		DynGenDef(\testDoneAction, "
            out0 = 1;
            elapsed += 1;
            elapsed == 256 ? doneAction(2);"
		).send;
		s.sync;
		{
			DynGen.ar(1, \testDoneAction, sync: 1.0);
		}.loadToFloatArray(0.01, action: { |sig|
			success = sig.every { |out, i|
				if (i < 256) { out == 1 } { out == 0 }
			};
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testVars: {
		var success = false;
		var condition = Condition();

		DynGenDef(\testVars, "out0=sampleNum;out1=blockNum;").send;
		s.sync;
		{
			DynGen.ar(2, \testVars, sync: 1.0);
		}.loadToFloatArray(0.01, action: {|sig|
			var blockSize = s.options.blockSize;
			success = true;
			sig.pairsDo { |a, b, i|
				var sampleIndex = i.div(2).mod(blockSize);
				var blockNum = i.div(2).div(blockSize);
				if (a != sampleIndex or: { b != blockNum }) { success = false; }
			};
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testConstants: {
		var success = false;
		var condition = Condition();

		DynGenDef(\testConstants, "out0=srate;out1=blockSize;").send;
		s.sync;
		{
			DynGen.ar(2, \testConstants, sync: 1.0);
		}.loadToFloatArray(0.01, action: {|sig|
			var sr = s.options.sampleRate;
			var bs = s.options.blockSize;
			success = sig[0] == sr and: { sig[1] == bs };
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testDelete: {
		var playSuccess = false;
		var deleteSuccess = false;
		var sclangSuccess = false;
		var reAddSuccess = false;

		var condition = Condition();
		var deleteCondition = Condition();
		var reAddCondition = Condition();

		DynGenDef(\testDelete, "out0=$pi").send;
		s.sync;
		{
			DynGen.ar(1, \testDelete, sync: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			playSuccess = (sig[0]-pi).abs<0.01;
			condition.unhang;
		});
		condition.hang;

		DynGenDef(\testDelete).free;
		sclangSuccess = DynGenDef.all.includesKey(\testDelete).not;

		{
			DynGen.ar(1, \testDelete, sync: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			deleteSuccess = (sig[0]).abs<0.01;
			deleteCondition.unhang;
		});
		deleteCondition.hang;

		// now add same dyngen code again
		DynGenDef(\testDelete, "out0=$pi").send;
		s.sync;
		{
			DynGen.ar(1, \testDelete, sync: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			reAddSuccess = (sig[0]-pi).abs<0.01;
			reAddCondition.unhang;
		});
		reAddCondition.hang;

		playSuccess.and(deleteSuccess).and(sclangSuccess).and(reAddSuccess);
	},

	testDeleteWhileRunning: {
		var synth;
		var bus = Bus.control(s, 1);
		var playSuccess = false;
		var deleteSuccess = false;

		var condition = Condition();
		var deleteCondition = Condition();

		DynGenDef(\testDeleteRunning, "out0=$pi").send;
		s.sync;
		synth = {A2K.kr(DynGen.ar(1, \testDeleteRunning, sync: 1.0))}.play(outbus: bus);
		s.sync;

		DynGenDef(\testDeleteRunning).free;
		s.sync;

		{
			DynGen.ar(1, \testDeleteRunning, sync: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			deleteSuccess = (sig[0]).abs<0.01;
			deleteCondition.unhang;
		});
		deleteCondition.hang;

		s.sync;

		bus.get({|signal|
			playSuccess = (signal - pi).abs < 0.01;
			condition.unhang;
		});
		condition.hang;

		synth.free;
		bus.free;

		playSuccess.and(deleteSuccess);
	},

	testDeleteAll: {
		var deleteSuccess = false;
		var playSuccess = false;
		var sclangSuccess = false;

		var deleteCondition = Condition();
		var playCondition = Condition();

		DynGenDef(\testDeleteAll, "out0=$pi").send;
		s.sync;
		DynGenDef.freeAll;
		s.sync;
		{
			DynGen.ar(1, \testDeleteAll, sync: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			deleteSuccess = (sig[0]).abs<0.01;
			deleteCondition.unhang;
		});
		deleteCondition.hang;

		sclangSuccess = DynGenDef.all.size == 0;

		DynGenDef(\testDeleteAll, "out0=$pi").send;
		s.sync;
		{
			DynGen.ar(1, \testDeleteAll, sync: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			playSuccess = (sig[0]-pi).abs<0.01;
			playCondition.unhang;
		});
		playCondition.hang;

		deleteSuccess.and(playSuccess);
	},

	testInputInitSection: {
		// in the @init section, in0, in1, etc. should be initialized to the first input sample
		var condition = Condition();
		var success = false;
		// this code outputs the first derivative of the inputs
		DynGenDef(\testInputInitSection, "
            @init
            prev0 = in0; prev1 = in1;
            @sample
            out0 = in0 - prev0; prev0 = in0;
            out1 = in1 - prev1; prev1 = in1;"
		).send;
		s.sync;
		{
			DynGen.ar(2, \testInputInitSection, LFNoise1.ar([100, 200]), sync: 1.0);
		}.loadToFloatArray(0.01, action: {|sig|
			// the first sample must be 0.0 in both channels
			success = sig[0..1].every(_ == 0.0);
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testInputBlockSection: {
		// in the @block section in0, in1, etc. should be initialized to the first input sample
		var condition = Condition();
		var success = false;
		DynGenDef(\testInputBlockSection, "
            @block
            x = in0;
            @sample
            out0 = in0;
            out1 = x;"
		).send;
		s.sync;
		{
			DynGen.ar(2, \testInputBlockSection, LFNoise1.ar(200), sync: 1.0);
		}.loadToFloatArray(0.01, action: {|sig|
			var blockSize = s.options.blockSize;
			success = true;
			// the two outputs should be equal at the beginning of every block.
			sig.pairsDo { |a, b, i|
				if ((i.div(2) % blockSize) == 0) {
					if (a != b) { success = false }
				}
			};
			condition.unhang;
		});
		condition.hang;
		success;

	},

	testParamsInitSection: {
		// \ir, \kr and \ar parameters should all have the same value in the @init section.
		var condition = Condition();
		var success = false;
		var value = 0.5;
		DynGenDef(\testParamsInitSection, "
            @init
            x = _x; y = _y; z = _z;
            @sample
            out0 = x; out1 = y; out2 = z;"
		).send;
		s.sync;
		{
			DynGen.ar(3, \testParamsInitSection,
				params: [ x: value, y: \a.kr(value), z: K2A.ar(value) ], sync: 1.0);
		}.loadToFloatArray(0.01, action: {|sig|
			success = sig[0..2].every(_ == value);
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testParamsBlockSection: {
		var condition = Condition();
		var success = false;
		// in the @block section, \kr parameters should have the same value as the corresponding \ar parameters.
		DynGenDef(\testParamsBlockSection, "
            @block
            x = _x; y = _y;
            @sample
            out0 = x; out1 = y;"
		).send;
		s.sync;
		{
			var sig = WhiteNoise.ar;
			DynGen.ar(2, \testParamsBlockSection,
				params: [ x: A2K.kr(sig), y: sig ], sync: 1.0);
		}.loadToFloatArray(0.02, action: {|sig|
			var maxErr = 0.0;
			sig.pairsDo { |x, y|
				var err = abs(x - y);
				maxErr = max(maxErr, err);
			};
			success = maxErr < 0.000001;
			condition.unhang;
		});
		condition.hang;
		success;
	},

	// signal function tests

	testClip: {
		var successClip = false;
		var successClip2 = false;

		var clipCondition = Condition();
		var clip2Condition = Condition();

		var data = (-3, -2.9 .. 3.0);
		var buffer = Buffer.loadCollection(s, data);
		DynGenDef(\testClip2, "out0=clip(in0, 1/$pi)").send;
		s.sync;
		{
			DynGen.ar(1, \testClip2, PlayBuf.ar(1, buffer, doneAction: Done.freeSelf), sync: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			successClip2 = data.collect({|x, i|
				(sig[i]-(x.clip2(1/pi))).abs < 0.001;
			}).every(_==true);
			clip2Condition.unhang;
		});
		clip2Condition.hang;

		DynGenDef(\testClip, "out0=clip(in0, 1/$pi, 0.5)").send;
		s.sync;
		{
			DynGen.ar(1, \testClip, PlayBuf.ar(1, buffer, doneAction: Done.freeSelf), sync: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			successClip = data.collect({|x, i|
				(sig[i]-(x.clip(1/pi, 0.5))).abs < 0.001;
			}).every(_==true);
			clipCondition.unhang;
		});
		clipCondition.hang;

		buffer.free;

		successClip.and(successClip2);
	},

	testWrap: {
		var successWrap = false;
		var successWrap2 = false;

		var wrapCondition = Condition();
		var wrap2Condition = Condition();

		var data = (-3, -2.9 .. 3.0);
		var buffer = Buffer.loadCollection(s, data);
		DynGenDef(\testWrap2, "out0=wrap(in0, 1/$pi)").send;
		s.sync;
		{
			DynGen.ar(1, \testWrap2, PlayBuf.ar(1, buffer, doneAction: Done.freeSelf), sync: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			successWrap2 = data.collect({|x, i|
				(sig[i]-(x.wrap2(1/pi))).abs < 0.001;
			}).every(_==true);
			wrap2Condition.unhang;
		});
		wrap2Condition.hang;

		DynGenDef(\testWrap, "out0=wrap(in0, 1/$pi, 0.5)").send;
		s.sync;
		{
			DynGen.ar(1, \testWrap, PlayBuf.ar(1, buffer, doneAction: Done.freeSelf), sync: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			successWrap = data.collect({|x, i|
				(sig[i]-(x.wrap(1/pi, 0.5))).abs < 0.001;
			}).every(_==true);
			wrapCondition.unhang;
		});
		wrapCondition.hang;

		buffer.free;

		successWrap.and(successWrap2);
	},

	testFold: {
		var successFold = false;
		var successFold2 = false;

		var foldCondition = Condition();
		var fold2Condition = Condition();

		var data = (-3, -2.9 .. 3.0);
		var buffer = Buffer.loadCollection(s, data);
		DynGenDef(\testFold2, "out0=fold(in0, 1/$pi)").send;
		s.sync;
		{
			DynGen.ar(1, \testFold2, PlayBuf.ar(1, buffer, doneAction: Done.freeSelf), sync: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			successFold2 = data.collect({|x, i|
				(sig[i]-(x.fold2(1/pi))).abs < 0.001;
			}).every(_==true);
			fold2Condition.unhang;
		});
		fold2Condition.hang;

		DynGenDef(\testFold, "out0=fold(in0, 1/$pi, 0.5)").send;
		s.sync;
		{
			DynGen.ar(1, \testFold, PlayBuf.ar(1, buffer, doneAction: Done.freeSelf), sync: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			successFold = data.collect({|x, i|
				(sig[i]-(x.fold(1/pi, 0.5))).abs < 0.001;
			}).every(_==true);
			foldCondition.unhang;
		});
		foldCondition.hang;

		buffer.free;

		successFold.and(successFold2);
	},

	testMod: {
		var success = false;

		var condition = Condition();

		var data = (-3, -2.9 .. 3.0);
		var buffer = Buffer.loadCollection(s, data);
		DynGenDef(\testMod, "out0=mod(in0, 1/$pi)").send;
		s.sync;
		{
			DynGen.ar(1, \testMod, PlayBuf.ar(1, buffer, doneAction: Done.freeSelf), sync: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			success = data.collect({|x, i|
				(sig[i]-(x.mod(1/pi))).abs < 0.001;
			}).every(_==true);
			condition.unhang;
		});
		condition.hang;

		buffer.free;

		success;
	},

	testLin: {
		var lin = {|x, a, b| a + (x*(b-a));};
		var success = false;

		var condition = Condition();

		var data = (-3, -2.9 .. 3.0);
		var buffer = Buffer.loadCollection(s, data);
		DynGenDef(\testLin, "out0=lin(0.5, in0, 3.0)").send;
		s.sync;
		{
			DynGen.ar(1, \testLin, PlayBuf.ar(1, buffer, doneAction: Done.freeSelf), sync: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			success = data.collect({|x, i|
				(sig[i]-lin.(0.5, x, 3.0)).abs < 0.1;
			}).every(_==true);
			condition.unhang;
		});
		condition.hang;

		buffer.free;

		success;
	},

	testCubic: {
		var success = false;

		var condition = Condition();

		DynGenDef(\testCubic, "out0=cubic(0.33, 1.0, 2.0, 3.0, 4.0, 5.0)").send;
		s.sync;
		{
			DynGen.ar(1, \testCubic, sync: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			success = (sig.first - 2.3299999237061) < 0.01;
			condition.unhang;
		});
		condition.hang;

		success;
	},

	// state function tests

	// this uses a number as state variable - which should not be done
	// results in [-0.4, 0.1, 0.1, 0.1, ...]
	testWrongDelta: {
		var success = false;
		var condition = Condition();

		DynGenDef(\testWrongDelta, "
out0 = delta(0.5, in0);
").send;
		s.sync;
		{
			DynGen.ar(1, \testWrongDelta, Integrator.ar(DC.ar(0.1)), sync: 1.0);
		}.loadToFloatArray(0.05, action: {|sig|
			success = (sig.first + 0.4) < 0.01;
			sig = sig.drop(1);
			success = success.and(sig.every({|x| (x-0.1) < 0.01}));
			condition.unhang;
		});
		condition.hang;

		success;
	},

	// this uses the input as state variable - which should not be done
	// results in [0.0, 0.0, 0.0, ...]
	testWrongDeltaInput: {
		var success = false;
		var condition = Condition();

		DynGenDef(\testWrongDeltaInput, "
out0 = delta(in0, in0);
").send;
		s.sync;
		{
			DynGen.ar(1, \testWrongDeltaInput, Integrator.ar(DC.ar(0.1)), sync: 1.0);
		}.loadToFloatArray(0.05, action: {|sig|
			success = sig.every({|x| x < 0.01});
			condition.unhang;
		});
		condition.hang;

		success;
	},

	// this uses the output as state variable - which should not be done
	// results in [0.1, 0.1, 0.2, 0.2, 0.3, 0.3, ...]
	testWrongDeltaOutput: {
		var success = false;
		var condition = Condition();

		DynGenDef(\testWrongDeltaOutput, "
out0 = delta(out0, in0);
").send;
		s.sync;
		{
			DynGen.ar(1, \testWrongDeltaOutput, Integrator.ar(DC.ar(0.1)), sync: 1.0);
		}.loadToFloatArray(0.05, action: {|sig|
			success = sig.every({|x, i| ((((i+2)/2.0).floor * 0.1) - x) < 0.01});
			condition.unhang;
		});
		condition.hang;

		success;
	},

	testDelta: {
		var success = false;
		var condition = Condition();

		DynGenDef(\testDelta, "
out0 = delta(x, in0);
").send;
		s.sync;
		{
			DynGen.ar(1, \testDelta, Integrator.ar(DC.ar(0.1)), sync: 1.0);
		}.loadToFloatArray(0.05, action: {|sig|
			success = sig.every({|x| (x-0.1) < 0.001});
			condition.unhang;
		});
		condition.hang;

		success;
	},

	testHistory: {
		var success = false;
		var condition = Condition();

		DynGenDef(\testHistory, "
out0 = history(x, in0) - in0;
").send;
		s.sync;
		{
			DynGen.ar(1, \testHistory, Integrator.ar(DC.ar(0.1)), sync: 1.0);
		}.loadToFloatArray(0.05, action: {|sig|
			success = sig.every({|x| (x-0.1) < 0.001});
			condition.unhang;
		});
		condition.hang;

		success;
	},

	testLatch: {
		var success = false;
		var condition = Condition();

		DynGenDef(\testLatch, "
out0 = latch(x, in0, _trigger);
").send;
		s.sync;
		{
			DynGen.ar(1, \testLatch, LFPulse.ar(800.0), [trigger: Impulse.ar(800.0)], sync: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			success = sig.every({|x| (x-1.0) < 0.001});
			condition.unhang;
		});
		condition.hang;

		success;
	},

	// sclang tests

	testRemoveComments: {
		var code = "
hello; //world
/*
and nothing
here
*/
you;
";
		var extract = DynGenDef.prRemoveComments(code);
		var inline = extract.contains("world").not;
		var multiLine = extract.contains("nothing").not;
		var afterLine = extract.contains("you;");
		inline.and(multiLine).and(afterLine);
	},

	testExtractParameters: {
		var code = "
out0 = in0*(_gain*_gain);
/*
_ignore = _this;
*/
out1 = __gain2;
";
		var params = DynGenDef.prExtractParameters(code);
		var gain = params.includes(\_gain);
		var gain2 = params.includes(\__gain2);
		var size = params.size == 2;
		gain.and(gain2).and(size);
	},

	// meta

	run: {|self, name=nil|
		var testsToRun = if(name.isNil, {self.allTests}, {name.asArray});
		var results = testsToRun.collect({|testName|
			"Running %".format(testName).postln;
			if (self[testName].isNil) { Error("'%' not found".format(testName)).throw };
			[testName, self.perform(testName)];
		});
		if(results.any(_[1].not), {
			"ERROR: There were errors".postln;
			results.select(_[1].not).do({|test|
				"ERROR: Test % failed!".format(test[0]).postln;
			});
		}, {
			"All tests ran successfully!".postln;
		});

	},
)
)

s.reboot;

fork{~dynGenTests.run};
