DynGenDef {
	classvar <all;
	var <name;
	var <hash;
	var <>code;

	// private
	var <prParams;

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
		^this.new(name).load(path);
	}

	load {|path|
		try {
			code = File.readAllString(path);
		} {
			^Error("DynGenDef: could not open file '%'".format(path)).throw;
		}
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

	sendMsg {|completionMsg|
		var message = [
			\cmd,
			\dyngenscript,
			hash,
			code,
			prParams.size,
		];
		message = message ++ prParams;
		message = message.add(completionMsg);
		^message;
	}

	free {|server|
		var message = this.freeMsg.asRawOSC;
		var servers = (server ?? { Server.allBootedServers }).asArray;
		servers.do({|each|
			if(each.hasBooted.not, {
				"Server % not running, can not free DynGenDef %.".format(server.name, name).warn;
			});
			each.sendRaw(message);
		});
		all[name] = nil;
	}

	freeMsg {
		^[
			\cmd,
			\dyngenfree,
			hash.asInteger,
		];
	}

	*freeAll {|server|
		var message = DynGenDef.freeAllMsg.asRawOSC;
		var servers = (server ?? { Server.allBootedServers }).asArray;
		servers.do({|each|
			if(each.hasBooted.not, {
				"Server % not running, can not free DynGenDef %.".format(server.name, name).warn;
			});
			each.sendRaw(message);
		});
		all = IdentityDictionary();
	}

	*freeAllMsg {
		^[
			\cmd,
			\dyngenfreeall,
		];
	}

	// this function adds the parameters to the
	// prParams array in an append only manner.
	// append only is necessary b/c we want to also
	// update already running instances which expect
	// a parameter under a given index.
	prRegisterParams {
		var params = DynGenDef.prExtractParameters(code);
		params.do({|param|
			if(prParams.indexOf(param).isNil, {
				prParams = prParams.add(param);
			});
		});
	}

	prSendScript {|server, completionMsg|
		var message = this.sendMsg(completionMsg).asRawOSC;
		if(message.size < (65535 div: 4), {
			server.sendRaw(message);
		}, {
			this.prSendFile(server, completionMsg);
		});
	}

	prSendFile {|server, completionMsg|
		var message;
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

		message = [
			\cmd,
			\dyngenfile,
			hash,
			tmpFilePath,
			prParams.size,
		];
		message = message ++ prParams;
		message.add(completionMsg);
		server.listSendMsg(message);

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
		var params = [];
		var regex = "[^(?:A-Za-z|\\_|$|]?(\_(?:[A-Za-z]|[0-9]|_)+)";
		var results = DynGenDef.prRemoveComments(code).findRegexp(regex);
		// regex returns match and group - we are only interested in the group
		results.pairsDo({|match, group|
			var name = group[1].asSymbol;
			// filter out duplicates
			if (params.includes(name).not, {
				params = params.add(name);
			});
		});
		^params;
	}

	// takes an array of [\paramName, signal] which should be
	// transformed to its numerical representation of the `prParameters`
	// array. Non existing parameters will be thrown away,
	// also only the first occurence of a parameter will be considered
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

	*prRemoveComments {|code|
		// dyngen code does not allow for strings,
		// so we can get away with using regex here
		// remove single line comments //
		code = code.replaceRegexp("\/\/.*?$", "");
		// remove multi line comments /* */
		code = code.replaceRegexp("\\/\\*.*?\\*\\/", "");
		^code;
	}
}

DynGen : MultiOutUGen {
	*ar {|numOutputs, script, inputs, params, update=1.0, sync=0.0|
		var signals;

		script = case
		{script.isKindOf(DynGenDef)} {script}
		{script.isKindOf(String)} {DynGenDef(script.asSymbol)}
		{script.isKindOf(Symbol)} {DynGenDef(script)}
		{Error("Script input needs to be a DynGenDef object or a symbol, found %".format(script.class)).throw};

		inputs = inputs.asArray;
		params = params.asArray;

		if(params.size.odd, {
			Error("Parameters need to be key-value pairs, but found an odd number of elements").throw;
		});

		signals = inputs ++ params;

		^this.multiNew(
			'audio',
			numOutputs,
			script,
			update,
			sync,
			inputs.size,
			params.size/2.0,  // parameters are tuples of [id, value]
			*signals,
		);
	}

	init { |numOutputs, script, update, sync, numInputs, numParams ... signals|
		var audioInputs = signals[..(numInputs-1)];
		var params = signals[numInputs..];

		params.pairsDo({|key, value|
			if(key.isKindOf(String).or(key.isKindOf(Symbol)).not, {
				Error("'%' is not a valid parameter key".format(key)).throw;
			});
			if(value.isValidUGenInput.not, {
				Error("Parameter '%' has invalid value '%'".format(key, value)).throw;
			});
		});

		params = script.prTranslateParameters(params);
		// update the parameter count because parameters might have been ignored!
		numParams = params.size.div(2);

		// signals must be audio rate
		audioInputs = audioInputs.collect({|sig|
			if(sig.rate != \audio, {
				K2A.ar(sig);
			}, {
				sig;
			});
		});

		// inputs is a member variable of UGen
		inputs = [
			script.hash.asFloat,
			update,
			sync,
			numInputs,
			numParams,
		] ++ audioInputs ++ params;

		^this.initOutputs(numOutputs, \audio);
	}
}
