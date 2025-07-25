// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// Functions that handle capturing QLOG traces.

use neqo_common::qlog::Qlog;
use neqo_transport::StreamId;
use qlog::events::{DataRecipient, EventData};

/// Uses [`Qlog::add_event_data_now`] instead of
/// [`Qlog::add_event_data_with_instant`], given that `now` is not available
/// on call-site. See docs on [`Qlog::add_event_data_now`] for details.
pub fn h3_data_moved_up(qlog: &Qlog, stream_id: StreamId, amount: usize) {
    qlog.add_event_data_now(|| {
        let ev_data = EventData::DataMoved(qlog::events::quic::DataMoved {
            stream_id: Some(stream_id.as_u64()),
            offset: None,
            length: Some(u64::try_from(amount).expect("usize fits in u64")),
            from: Some(DataRecipient::Transport),
            to: Some(DataRecipient::Application),
            raw: None,
        });

        Some(ev_data)
    });
}

/// Uses [`Qlog::add_event_data_now`] instead of
/// [`Qlog::add_event_data_with_instant`], given that `now` is not available
/// on call-site. See docs on [`Qlog::add_event_data_now`] for details.
pub fn h3_data_moved_down(qlog: &Qlog, stream_id: StreamId, amount: usize) {
    qlog.add_event_data_now(|| {
        let ev_data = EventData::DataMoved(qlog::events::quic::DataMoved {
            stream_id: Some(stream_id.as_u64()),
            offset: None,
            length: Some(u64::try_from(amount).expect("usize fits in u64")),
            from: Some(DataRecipient::Application),
            to: Some(DataRecipient::Transport),
            raw: None,
        });

        Some(ev_data)
    });
}
