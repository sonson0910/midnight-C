import { CompiledContract, ContractExecutable, type Contract } from '@midnight-ntwrk/compact-js/effect';
import { Contract as HelloWorld_ } from './managed/hello/contract/index.js';

type PrivateState = Record<string, never>;
type HelloWorldContract = HelloWorld_<PrivateState>;
const HelloWorldContract = HelloWorld_;

const witnesses: Contract.Contract.Witnesses<HelloWorldContract> = {};
const createInitialPrivateState = (): PrivateState => ({});

export default {
  contractExecutable: CompiledContract.make<HelloWorldContract>('HelloWorld', HelloWorldContract).pipe(
    CompiledContract.withWitnesses(witnesses),
    CompiledContract.withCompiledFileAssets('./managed/hello'),
    ContractExecutable.make
  ),
  createInitialPrivateState,
  config: {
    network: process.env.MIDNIGHT_NETWORK ?? 'preview',
    keys: {
      coinPublic: process.env.MIDNIGHT_LIVE_COIN_PUBLIC ?? ''
    }
  }
};

