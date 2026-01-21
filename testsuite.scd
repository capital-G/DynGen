(
~dynGenTests = (
	allTests: [
		\testBasic,
		\testRt,
		\testUpdate,
		\testNonExisting,
		\testParam,
		\testMultiParam,
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
		DynGenDef(\testUpdate, "out0=0.5;").send;
		s.sync;
		fork{
			0.1.wait;
			DynGenDef(\testUpdate, "out0=$pi;").send;
		};
		{
			DynGen.ar(1, \testUpdate);
		}.loadToFloatArray(0.4, action: {|sig|
			success = sig.any({|x| (x-pi).abs < 0.01});
			condition.unhang;
		});
		condition.hang;
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

fork{~dynGenTests.run}
