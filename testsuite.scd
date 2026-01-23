(
~dynGenTests = (
	allTests: [
		\testBasic,
		\testRt,
		\testUpdate,
		\testNonExisting,
		\testDynamicIO,
		\testParam,
		\testMultiParam,
		\testBufRead,
		\testBufWrite,
		\testConstants,
		\testDelete,
		\testDeleteWhileRunning,
		\testDeleteAll,

		// signal function tests
		\testClip,
		\testWrap,
		\testFold,
		\testMod,
		\testLin,
		\testCubic,

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

	testRt: {
		var success = false;
		var condition = Condition();
		DynGenDef(\testRt, "out0=0.5;").send;
		s.sync;
		{
			DynGen.ar(1, \testRt, realtime: 1.0);
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
		success = checkUpdate.(true) and: { checkUpdate.(false).not };
		success;
	},

	testNonExisting: {
		var success = false;
		var condition = Condition();
		{
			DynGen.ar(1, \nonExisting);
		}.loadToFloatArray(0.1, action: {|sig|
			success = sig.any({|x| (x-0.0).abs < 0.01});
			condition.unhang;
		});
		condition.hang;
		success;
	},

	testDynamicIO: {
		var success = false;
		var condition = Condition();
		DynGenDef(\testDynamicIO, "out(0)=0.5;out(1)=in(0);").send;
		s.sync;
		{
			DynGen.ar(2, \testDynamicIO, DC.ar(20.0), realtime: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			var left = (sig[0]-0.5).abs < 0.01;
			var right = (sig[1]-20.0).abs < 0.01;
			success = left.and(right);
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

	testConstants: {
		var success = false;
		var condition = Condition();

		DynGenDef(\testConstants, "out0=srate;out1=blockSize;").send;
		s.sync;
		{
			DynGen.ar(1, \testConstants, realtime: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			var sr = sig[0] > 40000;
			var block = sig[1] > 4;
			success = sr.and(block);
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
			DynGen.ar(1, \testDelete, realtime: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			playSuccess = (sig[0]-pi).abs<0.01;
			condition.unhang;
		});
		condition.hang;

		DynGenDef(\testDelete).free;
		sclangSuccess = DynGenDef.all.includesKey(\testDelete).not;

		{
			DynGen.ar(1, \testDelete, realtime: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			deleteSuccess = (sig[0]).abs<0.01;
			deleteCondition.unhang;
		});
		deleteCondition.hang;

		// now add same dyngen code again
		DynGenDef(\testDelete, "out0=$pi").send;
		s.sync;
		{
			DynGen.ar(1, \testDelete, realtime: 1.0);
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
		synth = {A2K.kr(DynGen.ar(1, \testDeleteRunning, realtime: 1.0))}.play(outbus: bus);
		s.sync;

		DynGenDef(\testDeleteRunning).free;
		s.sync;

		{
			DynGen.ar(1, \testDeleteRunning, realtime: 1.0);
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
			DynGen.ar(1, \testDeleteAll, realtime: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			deleteSuccess = (sig[0]).abs<0.01;
			deleteCondition.unhang;
		});
		deleteCondition.hang;

		sclangSuccess = DynGenDef.all.size == 0;

		DynGenDef(\testDeleteAll, "out0=$pi").send;
		s.sync;
		{
			DynGen.ar(1, \testDeleteAll, realtime: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			playSuccess = (sig[0]-pi).abs<0.01;
			playCondition.unhang;
		});
		playCondition.hang;

		deleteSuccess.and(playSuccess);
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
			DynGen.ar(1, \testClip2, PlayBuf.ar(1, buffer, doneAction: Done.freeSelf), realtime: 1.0);
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
			DynGen.ar(1, \testClip, PlayBuf.ar(1, buffer, doneAction: Done.freeSelf), realtime: 1.0);
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
			DynGen.ar(1, \testWrap2, PlayBuf.ar(1, buffer, doneAction: Done.freeSelf), realtime: 1.0);
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
			DynGen.ar(1, \testWrap, PlayBuf.ar(1, buffer, doneAction: Done.freeSelf), realtime: 1.0);
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
			DynGen.ar(1, \testFold2, PlayBuf.ar(1, buffer, doneAction: Done.freeSelf), realtime: 1.0);
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
			DynGen.ar(1, \testFold, PlayBuf.ar(1, buffer, doneAction: Done.freeSelf), realtime: 1.0);
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
			DynGen.ar(1, \testMod, PlayBuf.ar(1, buffer, doneAction: Done.freeSelf), realtime: 1.0);
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
			DynGen.ar(1, \testLin, PlayBuf.ar(1, buffer, doneAction: Done.freeSelf), realtime: 1.0);
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
			DynGen.ar(1, \testCubic, realtime: 1.0);
		}.loadToFloatArray(0.1, action: {|sig|
			success = (sig.first - 2.3299999237061) < 0.01;
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
