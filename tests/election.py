# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the Apache 2.0 License.
from infra.network import PrimaryNotFound
import time
import math
import infra.network
import infra.proc
import infra.e2e_args
import infra.checker
import http
import suite.test_requirements as reqs

from ccf.tx_status import TxStatus
from ccf.log_capture import flush_info
from loguru import logger as LOG

# This test starts from a given number of nodes (hosts), commits
# a transaction, stops the current primary, waits for an election and repeats
# this process until no progress can be made (i.e. no primary can be elected
# as F > N/2).


@reqs.description("Stopping current primary and waiting for a new one to be elected")
@reqs.can_kill_n_nodes(1)
def test_kill_primary(network, args):
    primary, _ = network.find_primary()
    primary.stop()
    new_primary, new_term = network.wait_for_new_primary(primary.node_id)
    LOG.debug(f"New primary is {new_primary.node_id} in term {new_term}")

    return network


def wait_for_seqno_to_commit(seqno, view, nodes):
    """
    Wait for a specific seqno at a specific view to be committed on all nodes.
    """
    for _ in range(infra.network.Network.replication_delay * 10):
        up_to_date_f = []
        logs = {}
        for f in nodes:
            with f.client() as c:
                logs[f.node_id] = []
                r = c.get(
                    f"/node/tx?view={view}&seqno={seqno}", log_capture=logs[f.node_id]
                )
                assert (
                    r.status_code == http.HTTPStatus.OK
                ), f"tx request returned HTTP status {r.status_code}"
                status = TxStatus(r.body.json()["status"])
                if status == TxStatus.Committed:
                    up_to_date_f.append(f.node_id)
                elif status == TxStatus.Invalid:
                    flush_info(logs[f.node_id], None, 0)
                    raise RuntimeError(
                        f"Node {f.node_id} reports transaction ID {view}.{seqno} is invalid and will never be committed"
                    )
                else:
                    pass
        if len(up_to_date_f) == len(nodes):
            break
        time.sleep(0.1)
    for lines in logs.values():
        flush_info(lines, None, 0)
    assert len(up_to_date_f) == len(
        nodes
    ), "Only {} out of {} nodes are up to date".format(len(up_to_date_f), len(nodes))


def run(args):
    with infra.network.network(
        args.nodes, args.binary_dir, args.debug_nodes, args.perf_nodes, pdb=args.pdb
    ) as network:
        check = infra.checker.Checker()

        network.start_and_join(args)
        current_view = None

        # Number of nodes F to stop until network cannot make progress
        nodes_to_stop = math.ceil(len(args.nodes) / 2)
        if args.consensus == "bft":
            nodes_to_stop = math.ceil(len(args.nodes) / 3)

        primary_is_known = True
        for node_to_stop in range(nodes_to_stop):
            # Note that for the first iteration, the primary is known in advance anyway
            LOG.debug("Find freshly elected primary")
            # After a view change in bft, finding the new primary takes longer
            primary, current_view = network.find_primary(
                timeout=(30 if args.consensus == "bft" else 3)
            )

            LOG.debug(
                "Commit new transactions, primary:{}, current_view:{}".format(
                    primary.node_id, current_view
                )
            )
            with primary.client("user0") as c:
                res = c.post(
                    "/app/log/private",
                    {
                        "id": current_view,
                        "msg": "This log is committed in view {}".format(current_view),
                    },
                )
                check(res, result=True)
                seqno = res.seqno

            LOG.debug("Waiting for transaction to be committed by all nodes")
            wait_for_seqno_to_commit(seqno, current_view, network.get_joined_nodes())

            try:
                test_kill_primary(network, args)
            except PrimaryNotFound:
                if node_to_stop < nodes_to_stop - 1:
                    raise
                else:
                    primary_is_known = False

        assert not primary_is_known, "Primary is still known"
        LOG.success("Test ended successfully.")


if __name__ == "__main__":

    args = infra.e2e_args.cli_args()
    args.package = "liblogging"
    args.nodes = infra.e2e_args.min_nodes(args, f=1)
    run(args)
