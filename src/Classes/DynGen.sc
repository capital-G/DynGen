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

	add {|server|
		// @todo add a uuid to avoid clashes?
		var tmpFilePath = PathName.tmp ++ hash.asString;

		File.use(tmpFilePath, "w", {|f|
			f.write(code);
		});

		server = server ? Server.default;
		server.sendMsg(\cmd, \dyngenadd, hash, tmpFilePath);
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
	*ar {|numOutputs, scriptBuffer ...inputs|
		^super.ar(numOutputs, scriptBuffer, 0.0, *inputs);
	}

	init { |... theInputs|
		inputs = theInputs;
		^this.initOutputs(theInputs[0], 'audio');
	}
}

DynGenRT : MetaDynGen {
	*ar {|numOutputs, scriptBuffer ...inputs|
		^super.ar(numOutputs, scriptBuffer, 1.0, *inputs);
	}

	init { |... theInputs|
		inputs = theInputs;
		^this.initOutputs(theInputs[0], 'audio');
	}
}

MetaDynGen : MultiOutUGen {
	*ar {|numOutputs, script, realTime ...inputs|
	    script = if(script.class == DynGenDef, {
			script.hash;
		}, {
			DynGenDef.prHashSymbol(script.asSymbol);
		}).asFloat;
		^this.multiNew('audio', numOutputs, script, inputs.size, realTime, *inputs);
	}

	init { |... theInputs|
		^this.initOutputs(theInputs[0], 'audio');
	}

	*codeBuffer {|code, server=nil|
		var buffer;
		server = server ? Server.default;
		buffer = Buffer.loadCollection(server, code.ascii);
		^buffer;
	}
}
