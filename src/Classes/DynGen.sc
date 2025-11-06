DynGenDef {
	classvar <all;
	var <name;
	var <code;

	var <hash;

	*initClass {
		all = IdentityDictionary();
	}

	*new {|name, code|
		var res;
		if(code.isNil, {
			^all[name.asSymbol];
		}, {
			name = name.asSymbol;
			res = super.newCopyArgs(
				name,
				code.value.asString,
				DynGenDef.prHashSymbol(name),
			);
			all[res.name] = res;
			^res;
		});
	}

	*load {|name, path|
		var code;
		path = path.asAbsolutePath;
		if(File.exists(path).not, {
			"DynGen file % does not exist".format(path).warn;
			^nil;
		});
		code = File.readAllString(path);
		^this.new(name, code);
	}

	add {|server|
		// @todo add a uuid to avoid clashes?
		var tmpFilePath = PathName.tmp ++ hash.asString;

		File.use(tmpFilePath, "w", {|f|
			f.write(code);
		});

		server = server ? Server.default;
		server.sendMsg(\cmd, \dyngenadd, hash, tmpFilePath);

		fork {
			var deleteSuccess;
			server.sync;
			deleteSuccess = File.delete(tmpFilePath);
			if (deleteSuccess.not, {
				"Could not delete DynGen temp file %".format(tmpFilePath).warn;
			});
		}
	}

	*prHashSymbol {|symbol|
		// hash numbers are too high to represent as a float32
		// on the server, so we have to scale those down.
		// 2**20 seems okayish?
		^(symbol.hash.abs % (2**20) * symbol.hash.sign).asInteger;
	}
}

// UGen code

DynGen : MetaDynGen {
	*ar {|numOutputs, script ...inputs|
		^super.ar(numOutputs, script, 0.0, *inputs);
	}
}

DynGenRT : MetaDynGen {
	*ar {|numOutputs, script ...inputs|
		^super.ar(numOutputs, script, 1.0, *inputs);
	}
}

MetaDynGen : MultiOutUGen {
	*ar {|numOutputs, script, realTime ...inputs|
		script = if(script.class == DynGenDef, {
			script.hash;
		}, {
			DynGenDef.prHashSymbol(script.asSymbol);
		}).asFloat;
		^this.multiNew('audio', numOutputs, script, realTime, *inputs);
	}

	init { |numOutputs ... theInputs|
		inputs = theInputs;
		^this.initOutputs(numOutputs, 'audio');
	}
}
