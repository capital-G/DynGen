DynGenDef {
	classvar <all;
	var <name;
	var <hash;
	var <>code;

	// private
	var <>prParams;

	// private
	classvar counter;

	*initClass {
		all = IdentityDictionary();
		counter = 0;
	}

	*new {|name, code|
		var res;
		var hash;

		name = name.asSymbol;
		res = all[name];
		if(res.notNil, {
			if(code.isNil.not, {
				res.code = code;
			});
			^res;
		});
		hash = DynGenDef.prHashSymbol(name);
		res = super.newCopyArgs(name, hash, code ? "", []);
		all[name] = res;
		^res;
	}

	*load {|name, path|
		var code = File.readAllString(path.asAbsolutePath);
		^this.new(name, code);
	}

	load {|path|
		code = File.readAllString(path.asAbsolutePath);
	}

	send {|server, completionMsg|
		var servers = (server ?? { Server.allBootedServers }).asArray;
		this.prRegisterParams;
		servers.do { |each|
			if(each.hasBooted.not) {
				"Server % not running, could not send DynGenDef.".format(server.name).warn
			};
			this.prSendScript(each, completionMsg);
		}
	}

	prRegisterParams {
		var params = DynGenDef.prExtractParameters(code);
		params.do({|param|
			if(prParams.indexOf(param).isNil, {
				prParams = prParams.add(param);
			});
		});
	}

	prSendScript {|server, completionMsg|
		var message = [
			\cmd,
			\dyngenscript,
			hash,
			code,
			prParams.size,
		];
		message = message ++ prParams;
		message = message.add(completionMsg);
		message = message.asRawOSC;
		if(message.size < (65535 div: 4), {
			server.sendRaw(message);
		}, {
			this.prSendFile(server, completionMsg);
		});
	}

	prSendFile {|server, completionMsg|
		var tmpFilePath = PathName.tmp +/+ "%_%".format(hash.asString, counter);
		counter = counter + 1;

		if (server.isLocal.not, {
			"DynGenDef % could not be added  to server % because it is too big for sending via OSC and server is not local".format(
				name,
				server,
			).warn;
			^this;
		});

		File.use(tmpFilePath, "w", {|f|
			f.write(code);
		});

		server.sendMsg(\cmd, \dyngenfile, hash, tmpFilePath, completionMsg);

		fork {
			var deleteSuccess;
			server.sync;
			deleteSuccess = File.delete(tmpFilePath);
			if (deleteSuccess.not, {
				"Could not delete temp file % of DynGenDef %".format(tmpFilePath, name).warn;
			});
		}
	}

	*prHashSymbol {|symbol|
		// hash numbers are too high to represent as a float32
		// on the server, so we have to scale those down.
		// 2**20 seems okayish?
		^(symbol.hash.abs % (2**20) * symbol.hash.sign).asInteger;
	}

	// extracts variables in code that are prepended with a _
	// as these are considered parameter variables
	*prExtractParameters {|code|
		var regex = "[^(?:A-z|\\_|$)](\_(?:[A-z]|[0-9]|_)+)";
		var params = code.findRegexp(regex);
		// regex returns match and group - we are only interested in the group
		params = params.reject({|x, i| i.even});
		// and we are not interested in the position - so we also remove that
		params = params.collect({|x| x[1].asSymbol});
		^params;
	}

	// takes an array of [\paramName, signal] which should be
	// trasformed to its numerical representation of the `prParameters`
	// array. Non existing parameters will be thrown away
	prTranslateParameters {|parameters|
		var newParameters = [];
		parameters.pairsDo({|param, value|
			var index = prParams.indexOf("_%".format(param).asSymbol);
			if(index.notNil, {
				newParameters = newParameters.add(index).add(value);
			}, {
				"Parameter % is not registered in % - will be ignored".format(
					param,
					name,
				).warn;
			});
		});
		^newParameters;
	}
}

// UGen code

DynGen : MetaDynGen {
	*ar {|numOutputs, script ... inputs, parameters|
		^super.ar(numOutputs, script, 0.0, inputs, parameters);
	}
}

DynGenRT : MetaDynGen {
	*ar {|numOutputs, script ...inputs, parameters|
		^super.ar(numOutputs, script, 1.0, inputs, parameters);
	}
}

MetaDynGen : MultiOutUGen {
	*ar {|numOutputs, script, realTime, inputs, parameters|
		var signals;

		script = case
		{script.isKindOf(DynGenDef)} {script}
		{script.isKindOf(String)} {DynGenDef(script.asSymbol)}
		{script.isKindOf(Symbol)} {DynGenDef(script)}
		{Error("Script input needs to be a DynGenDef object or a symbol, found %".format(script.class)).throw};

		parameters = script.prTranslateParameters(parameters);

		signals = inputs ++ parameters;

		^this.multiNew(
			'audio',
			numOutputs,
			script.hash.asFloat,
			realTime,
			inputs.size,
			parameters.size/2.0,  // parameters are tuples of [id, value]
			*(signals).postln,
		);
	}

	init { |numOutputs ... theInputs|
		inputs = theInputs;
		inputs = inputs.asArray.collect({|input|
			if(input.rate != \audio, {
				K2A.ar(input);
			}, {
				input;
			});
		});
		^this.initOutputs(numOutputs, 'audio');
	}
}
