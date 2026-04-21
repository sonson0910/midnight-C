import type * as __compactRuntime from '@midnight-ntwrk/compact-runtime';

export type Witnesses<PS> = {
}

export type ImpureCircuits<PS> = {
  vestedAmount(context: __compactRuntime.CircuitContext<PS>, nowTs_0: bigint): __compactRuntime.CircuitResults<PS, bigint>;
  releasableAmount(context: __compactRuntime.CircuitContext<PS>, nowTs_0: bigint): __compactRuntime.CircuitResults<PS, bigint>;
  claim(context: __compactRuntime.CircuitContext<PS>,
        nowTs_0: bigint,
        amount_0: bigint,
        caller_0: { bytes: Uint8Array }): __compactRuntime.CircuitResults<PS, []>;
  revoke(context: __compactRuntime.CircuitContext<PS>,
         caller_0: { bytes: Uint8Array }): __compactRuntime.CircuitResults<PS, []>;
}

export type PureCircuits = {
}

export type Circuits<PS> = {
  vestedAmount(context: __compactRuntime.CircuitContext<PS>, nowTs_0: bigint): __compactRuntime.CircuitResults<PS, bigint>;
  releasableAmount(context: __compactRuntime.CircuitContext<PS>, nowTs_0: bigint): __compactRuntime.CircuitResults<PS, bigint>;
  claim(context: __compactRuntime.CircuitContext<PS>,
        nowTs_0: bigint,
        amount_0: bigint,
        caller_0: { bytes: Uint8Array }): __compactRuntime.CircuitResults<PS, []>;
  revoke(context: __compactRuntime.CircuitContext<PS>,
         caller_0: { bytes: Uint8Array }): __compactRuntime.CircuitResults<PS, []>;
}

export type Ledger = {
  readonly admin: { bytes: Uint8Array };
  readonly beneficiary: { bytes: Uint8Array };
  readonly startTs: bigint;
  readonly cliffTs: bigint;
  readonly durationSecs: bigint;
  readonly totalAmount: bigint;
  readonly releasedAmount: bigint;
  readonly revoked: boolean;
}

export type ContractReferenceLocations = any;

export declare const contractReferenceLocations : ContractReferenceLocations;

export declare class Contract<PS = any, W extends Witnesses<PS> = Witnesses<PS>> {
  witnesses: W;
  circuits: Circuits<PS>;
  impureCircuits: ImpureCircuits<PS>;
  constructor(witnesses: W);
  initialState(context: __compactRuntime.ConstructorContext<PS>,
               b_0: { bytes: Uint8Array },
               s_0: bigint,
               c_0: bigint,
               d_0: bigint,
               t_0: bigint): __compactRuntime.ConstructorResult<PS>;
}

export declare function ledger(state: __compactRuntime.StateValue | __compactRuntime.ChargedState): Ledger;
export declare const pureCircuits: PureCircuits;
