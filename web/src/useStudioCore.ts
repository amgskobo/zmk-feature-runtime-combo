import { useEffect, useState } from "react";
import { call_rpc } from "@zmkfirmware/zmk-studio-ts-client";
import type { RpcConnection } from "@zmkfirmware/zmk-studio-ts-client";
import type { KeyPhysicalAttrs } from "@zmkfirmware/zmk-studio-ts-client/keymap";

export type BehaviorOption = { id: number; displayName: string };

/**
 * Fetches the active physical layout's key positions via the standard ZMK
 * Studio core RPC (keymap.getPhysicalLayouts), so combo positions can be
 * picked from a rendered layout instead of typed in as raw numbers.
 */
export function usePhysicalLayoutKeys(
  connection: RpcConnection | null | undefined
): KeyPhysicalAttrs[] | null {
  const [result, setResult] = useState<{
    connection: RpcConnection;
    keys: KeyPhysicalAttrs[] | null;
  } | null>(null);

  useEffect(() => {
    if (!connection) {
      return undefined;
    }
    let cancelled = false;
    void Promise.resolve()
      .then(() =>
        call_rpc(connection, { keymap: { getPhysicalLayouts: true } })
      )
      .then(
        (resp) => {
          if (cancelled) return;
          const layouts = resp?.keymap?.getPhysicalLayouts;
          if (!layouts) return;
          const layout = layouts.layouts[layouts.activeLayoutIndex];
          setResult({ connection, keys: layout?.keys ?? null });
        },
        () => {
          if (!cancelled) setResult({ connection, keys: null });
        }
      );
    return () => {
      cancelled = true;
    };
  }, [connection]);

  return result && result.connection === connection ? result.keys : null;
}

/**
 * Fetches every behavior's id and display name via the standard ZMK Studio
 * core RPCs (behaviors.listAllBehaviors + getBehaviorDetails), so a combo's
 * behavior can be picked by name instead of a raw numeric id.
 */
export function useBehaviorOptions(
  connection: RpcConnection | null | undefined
): BehaviorOption[] | null {
  const [result, setResult] = useState<{
    connection: RpcConnection;
    options: BehaviorOption[] | null;
  } | null>(null);

  useEffect(() => {
    if (!connection) {
      return undefined;
    }
    let cancelled = false;
    void (async () => {
      const listResp = await call_rpc(connection, {
        behaviors: { listAllBehaviors: true },
      });
      if (cancelled) return;
      const ids = listResp?.behaviors?.listAllBehaviors?.behaviors ?? [];
      const details = await Promise.all(
        ids.map((id) =>
          call_rpc(connection, {
            behaviors: { getBehaviorDetails: { behaviorId: id } },
          })
        )
      );
      if (cancelled) return;
      const resolved = details
        .map((resp, index) => ({
          id: ids[index],
          displayName: resp?.behaviors?.getBehaviorDetails?.displayName ?? "",
        }))
        .filter((option) => option.displayName)
        .sort((a, b) => a.displayName.localeCompare(b.displayName));
      setResult({ connection, options: resolved });
    })().catch(() => {
      if (!cancelled) setResult({ connection, options: null });
    });
    return () => {
      cancelled = true;
    };
  }, [connection]);

  return result && result.connection === connection ? result.options : null;
}
