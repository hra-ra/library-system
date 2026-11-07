'use strict';

// ---------- helpers ----------
const $ = (sel, root = document) => root.querySelector(sel);
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
const fdate = (iso) => {
  if (!iso) return '';
  const [y, m, d] = iso.split('-');
  return `${+d} ${MONTHS[+m - 1]} ${y}`;
};
const money = (n) => 'Rs. ' + (Number.isInteger(+n) ? +n : (+n).toFixed(2));
const plural = (n, one, many) => `${n} ${n === 1 ? one : many || one + 's'}`;

let state = { me: null, token: 0 };

async function api(method, url, body) {
  const res = await fetch(url, {
    method,
    headers: body ? { 'Content-Type': 'application/json' } : undefined,
    body: body ? JSON.stringify(body) : undefined,
    credentials: 'same-origin',
  });
  let data = {};
  try { data = await res.json(); } catch (_) { /* empty body */ }
  if (res.status === 401 && url !== '/api/login') showLogin();
  if (!res.ok) throw new Error(data.error || 'Request failed');
  return data;
}

let toastTimer;
function toast(msg, isErr = false) {
  const el = $('#toast');
  el.textContent = msg;
  el.className = 'toast show' + (isErr ? ' err' : '');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => (el.className = 'toast'), isErr ? 5000 : 3200);
}

function statusBadge(i) {
  if (i.status === 'returned') return '<span class="badge done">Returned</span>';
  if (i.status === 'overdue') return `<span class="badge late">${plural(i.days_overdue, 'day')} late</span>`;
  if (i.days_left === 0) return '<span class="badge soon">Due today</span>';
  if (i.days_left === 1) return '<span class="badge soon">Due tomorrow</span>';
  if (i.days_left <= 3) return `<span class="badge soon">Due in ${i.days_left} days</span>`;
  return '<span class="badge out">Out</span>';
}

function table(headers, rows, emptyText) {
  if (!rows.length) return `<p class="empty">${esc(emptyText)}</p>`;
  const head = headers.map((h) => `<th${h.num ? ' class="num"' : ''}>${esc(h.label)}</th>`).join('');
  return `<div class="table-wrap"><table><thead><tr>${head}</tr></thead><tbody>${rows.join('')}</tbody></table></div>`;
}

// ---------- modal form ----------
function openForm({ title, fields, values = {}, submitLabel = 'Save', onSubmit, onDelete }) {
  const dlg = $('#dialog');
  const inputs = fields.map((f) => {
    const v = values[f.name] ?? f.value ?? '';
    const attrs = `name="${f.name}"${f.required ? ' required' : ''}${f.list ? ` list="${f.list}"` : ''}${f.placeholder ? ` placeholder="${esc(f.placeholder)}"` : ''}`;
    let control;
    if (f.options) {
      control = `<select ${attrs}>${f.options.map((o) => `<option value="${esc(o.value ?? o)}"${String(o.value ?? o) === String(v) ? ' selected' : ''}>${esc(o.label ?? o)}</option>`).join('')}</select>`;
    } else {
      control = `<input ${attrs} type="${f.type || 'text'}" value="${esc(v)}"${f.step ? ` step="${f.step}"` : ''}${f.min !== undefined ? ` min="${f.min}"` : ''}>`;
    }
    return `<label${f.wide ? ' style="grid-column:1/-1"' : ''}>${esc(f.label)}${control}${f.hint ? `<span class="hint">${esc(f.hint)}</span>` : ''}</label>`;
  }).join('');

  dlg.innerHTML = `<form method="dialog" novalidate>
    <h2>${esc(title)}</h2>
    <div class="two">${inputs}</div>
    <p class="form-error" role="alert"></p>
    <div class="foot">
      ${onDelete ? '<button type="button" class="btn danger spacer" data-act="delete">Delete</button>' : ''}
      <button type="button" class="btn" data-act="cancel">Cancel</button>
      <button type="submit" class="btn primary">${esc(submitLabel)}</button>
    </div></form>`;

  const form = $('form', dlg);
  const errBox = $('.form-error', dlg);
  $('[data-act=cancel]', dlg).onclick = () => dlg.close();
  if (onDelete) {
    $('[data-act=delete]', dlg).onclick = async () => {
      if (!confirm('Delete this record? This can\'t be undone.')) return;
      try { await onDelete(); dlg.close(); } catch (e) { errBox.textContent = e.message; }
    };
  }
  form.onsubmit = async (ev) => {
    ev.preventDefault();
    errBox.textContent = '';
    const data = Object.fromEntries(new FormData(form).entries());
    try { await onSubmit(data); dlg.close(); } catch (e) { errBox.textContent = e.message; }
  };
  dlg.showModal();
  const first = $('input, select', dlg);
  if (first) first.focus();
}

// ---------- pages ----------
const pages = {};

async function returnIssue(id, after) {
  try {
    const i = await api('POST', `/api/issues/${id}/return`);
    toast(i.fine > 0 ? `Returned. Fine of ${money(i.fine)} added for ${i.student}.` : `Returned. No fine for ${i.student}.`);
    if (after) after();
  } catch (e) { toast(e.message, true); }
}

pages.overview = async function (root) {
  const d = await api('GET', '/api/dashboard');
  const lateRows = d.overdue_list.map((i) => `<tr>
      <td>${esc(i.student)}<small>${esc(i.enrollment_no)}</small></td>
      <td>${esc(i.book)}<small>${esc(i.author)}</small></td>
      <td>${fdate(i.due_on)}</td>
      <td>${plural(i.days_overdue, 'day')}</td>
      <td class="num">${money(i.fine)}</td>
      <td><button class="row-btn" data-return="${i.id}">Mark returned</button></td></tr>`);
  const soonRows = d.due_soon_list.map((i) => `<tr>
      <td>${esc(i.student)}<small>${esc(i.enrollment_no)}</small></td>
      <td>${esc(i.book)}<small>${esc(i.author)}</small></td>
      <td>${fdate(i.due_on)}</td>
      <td>${statusBadge(i)}</td>
      <td><button class="row-btn" data-return="${i.id}">Mark returned</button></td></tr>`);

  root.innerHTML = `
    <div class="page-head"><div><h1>Overview</h1><p>${fdate(d.today)}</p></div>
      <div class="actions"><a class="btn primary" href="#/issue" style="text-decoration:none">Issue a book</a></div></div>
    <div class="strip">
      <div><div class="k">Books out</div><div class="v">${d.copies_out}</div><div class="sub">of ${d.copies} copies</div></div>
      <div><div class="k">Overdue</div><div class="v${d.overdue ? ' late' : ''}">${d.overdue}</div><div class="sub">${d.overdue ? money(d.accruing_fine_total) + ' fine so far' : 'nothing late'}</div></div>
      <div><div class="k">Due in 3 days</div><div class="v">${d.due_soon}</div><div class="sub">including today</div></div>
      <div><div class="k">Fines to collect</div><div class="v">${money(d.unpaid_fine_total)}</div><div class="sub">${plural(d.unpaid_fine_count, 'unpaid fine')}</div></div>
      <div><div class="k">Students</div><div class="v">${d.students}</div><div class="sub">${plural(d.titles, 'title')} in catalogue</div></div>
    </div>
    <section class="panel"><div class="panel-head"><h2>Overdue</h2><span class="muted">${d.overdue ? 'Oldest first' : ''}</span></div>
      ${table([{ label: 'Student' }, { label: 'Book' }, { label: 'Was due' }, { label: 'Late by' }, { label: 'Fine so far', num: true }, { label: '' }], lateRows, 'No overdue books today.')}
    </section>
    <section class="panel"><div class="panel-head"><h2>Due in the next 3 days</h2></div>
      ${table([{ label: 'Student' }, { label: 'Book' }, { label: 'Due' }, { label: '' }, { label: '' }], soonRows, 'Nothing falls due in the next 3 days.')}
    </section>`;
  root.addEventListener('click', (ev) => {
    const id = ev.target.dataset.return;
    if (id) returnIssue(id, () => route());
  });
};

pages.issue = async function (root) {
  const [students, books] = await Promise.all([api('GET', '/api/students'), api('GET', '/api/books')]);
  const active = students.filter((s) => s.status === 'active');
  const bookLabel = (b) => `${b.title} \u2014 ${b.author}`;
  let filter = 'open', q = '';

  root.innerHTML = `
    <div class="page-head"><div><h1>Issue &amp; return</h1><p>Type an enrollment number and pick a book. The due date is set from the loan period in Settings.</p></div></div>
    <section class="panel"><div class="panel-body">
      <form id="issue-form" class="issue-form" autocomplete="off">
        <label>Enrollment no.<input name="enrollment_no" list="students-list" required inputmode="numeric"></label>
        <label>Book<input name="book" list="books-list" required placeholder="Start typing a title"></label>
        <button class="btn primary" type="submit">Issue book</button>
        <p class="who" id="who-line"></p>
        <p class="form-error" id="issue-error" role="alert" style="grid-column:1/-1;margin:0"></p>
      </form>
      <datalist id="students-list">${active.map((s) => `<option value="${esc(s.enrollment_no)}">${esc(s.name)}</option>`).join('')}</datalist>
      <datalist id="books-list">${books.map((b) => `<option value="${esc(bookLabel(b))}">${b.available_copies ? b.available_copies + ' in' : 'none in'}</option>`).join('')}</datalist>
    </div></section>
    <section class="panel">
      <div class="panel-head"><h2>Loans</h2>
        <div class="actions">
          <div class="tabs" id="tabs"><button data-f="open" class="on">Out now</button><button data-f="returned">Returned</button><button data-f="all">All</button></div>
          <input class="search" id="q" placeholder="Search name, enrollment or title" aria-label="Search loans">
        </div></div>
      <div id="loans"></div>
    </section>`;

  const who = $('#who-line', root);
  const form = $('#issue-form', root);
  form.enrollment_no.addEventListener('input', () => {
    const s = students.find((x) => x.enrollment_no === form.enrollment_no.value.trim());
    who.textContent = s ? `${s.name}, ${s.department} year ${s.year}. ${plural(s.books_out, 'book')} out right now.` : '';
  });

  async function loadLoans() {
    const rows = await api('GET', `/api/issues?status=${filter}&q=${encodeURIComponent(q)}`);
    const trs = rows.map((i) => `<tr>
      <td>${esc(i.student)}<small>${esc(i.enrollment_no)}</small></td>
      <td>${esc(i.book)}<small>copy ${i.copy_no}</small></td>
      <td>${fdate(i.issued_on)}</td>
      <td>${fdate(i.due_on)}${i.returned_on ? `<small>back ${fdate(i.returned_on)}</small>` : ''}</td>
      <td>${statusBadge(i)}</td>
      <td class="num">${i.fine > 0 ? money(i.fine) + (i.status === 'returned' ? (i.fine_paid ? ' <span class="badge paid">paid</span>' : '') : '') : ''}</td>
      <td>${i.status === 'returned' ? '' : `<button class="row-btn" data-return="${i.id}">Return</button>`}</td></tr>`);
    $('#loans', root).innerHTML = table(
      [{ label: 'Student' }, { label: 'Book' }, { label: 'Issued' }, { label: 'Due' }, { label: 'Status' }, { label: 'Fine', num: true }, { label: '' }],
      trs, filter === 'open' ? 'No books are out right now.' : 'No loans match.');
  }

  form.onsubmit = async (ev) => {
    ev.preventDefault();
    const err = $('#issue-error', root);
    err.textContent = '';
    const label = form.book.value.trim();
    const book = books.find((b) => bookLabel(b) === label) || books.find((b) => b.title.toLowerCase() === label.toLowerCase());
    if (!book) { err.textContent = 'Pick a book from the suggestions so we know which title you mean.'; return; }
    try {
      const i = await api('POST', '/api/issues', { enrollment_no: form.enrollment_no.value.trim(), book_id: book.id });
      toast(`Issued "${i.book}" to ${i.student}. Due ${fdate(i.due_on)}.`);
      form.book.value = ''; form.enrollment_no.value = ''; who.textContent = '';
      form.enrollment_no.focus();
      book.available_copies = Math.max(0, book.available_copies - 1);
      loadLoans();
    } catch (e) { err.textContent = e.message; }
  };

  $('#tabs', root).onclick = (ev) => {
    if (!ev.target.dataset.f) return;
    filter = ev.target.dataset.f;
    root.querySelectorAll('#tabs button').forEach((b) => b.classList.toggle('on', b === ev.target));
    loadLoans();
  };
  let t;
  $('#q', root).oninput = (ev) => { clearTimeout(t); t = setTimeout(() => { q = ev.target.value.trim(); loadLoans(); }, 200); };
  root.addEventListener('click', (ev) => {
    const id = ev.target.dataset.return;
    if (id) returnIssue(id, loadLoans);
  });
  await loadLoans();
};

const DEPARTMENTS = ['IT', 'CSE', 'ECE', 'EEE', 'MAE'];

pages.students = async function (root) {
  let q = '';
  root.innerHTML = `
    <div class="page-head"><div><h1>Students</h1></div>
      <div class="actions"><input class="search" id="q" placeholder="Search name, enrollment, email" aria-label="Search students"><button class="btn primary" id="add">Add student</button></div></div>
    <section class="panel"><div id="list"></div></section>`;

  const fields = (editing) => [
    { name: 'enrollment_no', label: 'Enrollment no.', required: true },
    { name: 'name', label: 'Full name', required: true },
    { name: 'email', label: 'Email', type: 'email', required: true, wide: true },
    { name: 'phone', label: 'Phone', required: true, placeholder: '10 digits' },
    { name: 'department', label: 'Department', required: true, list: 'dept-list' },
    { name: 'year', label: 'Year', options: [1, 2, 3, 4].map((y) => ({ value: y, label: 'Year ' + y })) },
    ...(editing ? [{ name: 'status', label: 'Status', options: [{ value: 'active', label: 'Active' }, { value: 'inactive', label: 'Inactive' }] }] : []),
  ];

  async function load() {
    const rows = await api('GET', '/api/students?q=' + encodeURIComponent(q));
    window._students = rows;
    const trs = rows.map((s) => `<tr>
      <td>${esc(s.enrollment_no)}</td>
      <td>${esc(s.name)}<small>${esc(s.email)}</small></td>
      <td>${esc(s.department)}, year ${s.year}</td>
      <td>${esc(s.phone)}</td>
      <td class="num">${s.books_out}</td>
      <td>${s.status === 'active' ? '' : '<span class="badge done">Inactive</span>'}</td>
      <td><button class="row-btn" data-edit="${s.id}">Edit</button></td></tr>`);
    $('#list', root).innerHTML = table(
      [{ label: 'Enrollment no.' }, { label: 'Name' }, { label: 'Course' }, { label: 'Phone' }, { label: 'Books out', num: true }, { label: '' }, { label: '' }],
      trs, q ? 'No student matches that search.' : 'No students yet. Add the first one.');
  }

  const datalist = document.createElement('datalist');
  datalist.id = 'dept-list';
  datalist.innerHTML = DEPARTMENTS.map((d) => `<option value="${d}">`).join('');
  root.appendChild(datalist);

  $('#add', root).onclick = () => openForm({
    title: 'Add student', fields: fields(false), values: { year: 1 }, submitLabel: 'Add student',
    onSubmit: async (v) => { await api('POST', '/api/students', v); toast('Student added.'); load(); },
  });
  root.addEventListener('click', (ev) => {
    const id = ev.target.dataset.edit;
    if (!id) return;
    const s = window._students.find((x) => x.id == id);
    openForm({
      title: 'Edit student', fields: fields(true), values: s,
      onSubmit: async (v) => { await api('PUT', `/api/students/${id}`, v); toast('Changes saved.'); load(); },
      onDelete: async () => { await api('DELETE', `/api/students/${id}`); toast('Student deleted.'); load(); },
    });
  });
  let t;
  $('#q', root).oninput = (ev) => { clearTimeout(t); t = setTimeout(() => { q = ev.target.value.trim(); load(); }, 200); };
  await load();
};

pages.books = async function (root) {
  let q = '';
  root.innerHTML = `
    <div class="page-head"><div><h1>Books</h1></div>
      <div class="actions"><input class="search" id="q" placeholder="Search title, author, category" aria-label="Search books"><button class="btn primary" id="add">Add book</button></div></div>
    <section class="panel"><div id="list"></div></section>`;

  const fields = (editing) => [
    { name: 'title', label: 'Title', required: true, wide: true },
    { name: 'author', label: 'Author', required: true },
    { name: 'category', label: 'Category', list: 'cat-list' },
    { name: 'isbn', label: 'ISBN', hint: 'Optional' },
    { name: 'price', label: 'Price (Rs.)', type: 'number', min: 0, step: '1', hint: 'Caps the fine for this book' },
    editing ? { name: 'add_copies', label: 'Add more copies', type: 'number', min: 0, value: 0 }
            : { name: 'copies', label: 'Copies', type: 'number', min: 1, value: 1 },
  ];

  async function load() {
    const rows = await api('GET', '/api/books?q=' + encodeURIComponent(q));
    window._books = rows;
    const cats = [...new Set(rows.map((b) => b.category).filter(Boolean))];
    $('#cat-list', root).innerHTML = cats.map((c) => `<option value="${esc(c)}">`).join('');
    const trs = rows.map((b) => `<tr>
      <td>${esc(b.title)}<small>${esc(b.author)}</small></td>
      <td>${esc(b.category)}</td>
      <td>${esc(b.isbn) || '<span class="muted">\u2014</span>'}</td>
      <td class="num">${money(b.price)}</td>
      <td class="num">${b.available_copies} of ${b.total_copies}</td>
      <td><button class="row-btn" data-edit="${b.id}">Edit</button></td></tr>`);
    $('#list', root).innerHTML = table(
      [{ label: 'Title' }, { label: 'Category' }, { label: 'ISBN' }, { label: 'Price', num: true }, { label: 'On shelf', num: true }, { label: '' }],
      trs, q ? 'No book matches that search.' : 'No books yet. Add the first one.');
  }

  const dl = document.createElement('datalist');
  dl.id = 'cat-list';
  root.appendChild(dl);

  $('#add', root).onclick = () => openForm({
    title: 'Add book', fields: fields(false), submitLabel: 'Add book',
    onSubmit: async (v) => { await api('POST', '/api/books', v); toast('Book added.'); load(); },
  });
  root.addEventListener('click', (ev) => {
    const id = ev.target.dataset.edit;
    if (!id) return;
    const b = window._books.find((x) => x.id == id);
    openForm({
      title: 'Edit book', fields: fields(true), values: b,
      onSubmit: async (v) => { await api('PUT', `/api/books/${id}`, v); toast('Changes saved.'); load(); },
      onDelete: async () => { await api('DELETE', `/api/books/${id}`); toast('Book deleted.'); load(); },
    });
  });
  let t;
  $('#q', root).oninput = (ev) => { clearTimeout(t); t = setTimeout(() => { q = ev.target.value.trim(); load(); }, 200); };
  await load();
};

pages.fines = async function (root) {
  const rows = await api('GET', '/api/issues?status=fines');
  const due = rows.filter((i) => i.status === 'returned');
  const growing = rows.filter((i) => i.status !== 'returned');
  const sum = (a) => a.reduce((t, i) => t + i.fine, 0);

  const trs = rows.map((i) => `<tr>
    <td>${esc(i.student)}<small>${esc(i.enrollment_no)}</small></td>
    <td>${esc(i.book)}</td>
    <td>${fdate(i.due_on)}</td>
    <td>${i.returned_on ? fdate(i.returned_on) : '<span class="muted">not back yet</span>'}</td>
    <td class="num">${money(i.fine)}</td>
    <td>${i.status === 'returned' ? '<button class="row-btn" data-pay="' + i.id + '">Mark as paid</button>' : '<span class="badge late">still out, ' + plural(i.days_overdue, 'day') + ' late</span>'}</td></tr>`);

  root.innerHTML = `
    <div class="page-head"><div><h1>Fines</h1>
      <p>${money(sum(due))} to collect from returned books. ${money(sum(growing))} is building up on books that are still out.</p></div></div>
    <section class="panel">${table(
      [{ label: 'Student' }, { label: 'Book' }, { label: 'Was due' }, { label: 'Returned' }, { label: 'Fine', num: true }, { label: '' }],
      trs, 'No fines pending.')}</section>`;
  root.addEventListener('click', async (ev) => {
    const id = ev.target.dataset.pay;
    if (!id) return;
    try { await api('POST', `/api/issues/${id}/pay`); toast('Fine marked as paid.'); route(); } catch (e) { toast(e.message, true); }
  });
};

pages.reminders = async function (root) {
  const [settings, log] = await Promise.all([api('GET', '/api/settings'), api('GET', '/api/emails')]);
  const real = settings.email_sender === 'smtp';
  const days = settings.reminder_days.split(',').filter(Boolean).map(Number).sort((a, b) => b - a);
  const dayText = days.map((d) => (d === 0 ? 'on the due date' : d === 1 ? '1 day before' : d + ' days before')).join(', ');

  const logRows = log.map((e) => `<tr><td>${fdate(e.sent_on)}</td><td>${esc(e.student)}<small>${esc(e.recipient)}</small></td><td>${esc(e.book)}</td>
    <td>${esc(e.kind.replace('due_in_', 'due in ').replace('due_today', 'due today').replace('overdue', 'overdue notice'))}</td>
    <td>${e.status === 'sent' ? '<span class="badge paid">sent</span>' : '<span class="badge late">failed</span>'}</td></tr>`);

  root.innerHTML = `
    <div class="page-head"><div><h1>Reminders</h1><p>Emails go to students ${esc(dayText)}, then every few days once a book is overdue. Nobody gets the same reminder twice.</p></div></div>
    <section class="panel"><div class="panel-body">
      ${real ? '' : '<p style="margin-top:0"><strong>Email isn\'t connected yet.</strong> Messages are saved as text files in the <code>outbox</code> folder instead of being sent. To send for real, start the server with <code>SMTP_URL</code>, <code>SMTP_USER</code> and <code>SMTP_PASS</code> set.</p>'}
      <p class="muted" style="margin-top:0">The same check runs from the command line, so it can be scheduled every morning: <code>./reminder_job</code></p>
      <div class="actions"><button class="btn" id="preview">See who would get one today</button><button class="btn primary" id="send">Send today's reminders</button></div>
      <div id="result" style="margin-top:1rem"></div>
    </div></section>
    <section class="panel"><div class="panel-head"><h2>Recent emails</h2></div>${table(
      [{ label: 'Date' }, { label: 'Student' }, { label: 'Book' }, { label: 'Type' }, { label: 'Status' }], logRows, 'Nothing sent yet.')}</section>`;

  async function run(dry) {
    try {
      const r = await api('POST', '/api/reminders/run', { dry_run: dry });
      const trs = r.items.map((i) => `<tr><td>${esc(i.student)}<small>${esc(i.email)}</small></td><td>${esc(i.subject)}</td>
        <td>${dry ? '<span class="badge out">would send</span>' : i.sent ? '<span class="badge paid">sent</span>' : '<span class="badge late">failed: ' + esc(i.note) + '</span>'}</td></tr>`);
      $('#result', root).innerHTML = r.count
        ? table([{ label: 'Student' }, { label: 'Subject' }, { label: '' }], trs, '')
        : '<p class="muted" style="margin:0">Nobody needs a reminder right now.</p>';
      if (!dry && r.count) setTimeout(route, 1500);
    } catch (e) { toast(e.message, true); }
  }
  $('#preview', root).onclick = () => run(true);
  $('#send', root).onclick = () => run(false);
};

pages.settings = async function (root) {
  const s = await api('GET', '/api/settings');
  root.innerHTML = `
    <div class="page-head"><div><h1>Settings</h1></div></div>
    <form id="form" class="panel"><div class="panel-body settings-grid">
      <label>Library name<input name="library_name" value="${esc(s.library_name)}" required></label>
      <label>Loan period (days)<input name="loan_days" type="number" min="1" max="90" value="${s.loan_days}" required></label>
      <label>Books per student<input name="max_books" type="number" min="1" max="20" value="${s.max_books}" required></label>
      <label>Fine per day late (Rs.)<input name="fine_per_day" type="number" min="0" max="100" step="0.5" value="${s.fine_per_day}" required></label>
      <label>Fine rule<select name="fine_policy">
        <option value="capped"${s.fine_policy === 'capped' ? ' selected' : ''}>Daily rate, never more than the book's price</option>
        <option value="per_day"${s.fine_policy === 'per_day' ? ' selected' : ''}>Daily rate, no limit</option></select></label>
      <label>Send reminders<input name="reminder_days" value="${esc(s.reminder_days)}" required>
        <span class="hint">Days before the due date, e.g. 3,1,0</span></label>
      <p class="form-error" id="err" role="alert" style="grid-column:1/-1"></p>
      <div style="grid-column:1/-1"><button class="btn primary" type="submit">Save settings</button></div>
    </div></form>`;
  $('#form', root).onsubmit = async (ev) => {
    ev.preventDefault();
    try {
      await api('PUT', '/api/settings', Object.fromEntries(new FormData(ev.target).entries()));
      toast('Settings saved.');
      const me = await api('GET', '/api/me');
      $('#brand-name').textContent = me.library_name;
      $('#err').textContent = '';
    } catch (e) { $('#err').textContent = e.message; }
  };
};

// ---------- routing & session ----------
async function route() {
  const name = (location.hash.replace('#/', '') || 'overview');
  const page = pages[name] ? name : 'overview';
  document.querySelectorAll('#nav a').forEach((a) => a.classList.toggle('active', a.dataset.route === page));
  const root = $('#content');
  const myToken = ++state.token;
  const fresh = root.cloneNode(false);  // drops old event listeners
  root.replaceWith(fresh);
  fresh.innerHTML = '<p class="muted">Loading\u2026</p>';
  try {
    await pages[page](fresh);
    if (myToken !== state.token) return;
  } catch (e) {
    if (myToken === state.token) fresh.innerHTML = `<p class="form-error">${esc(e.message)}</p>`;
  }
}

function showLogin() {
  state.me = null;
  $('#app').hidden = true;
  $('#login').hidden = false;
}

async function showApp(me) {
  state.me = me;
  $('#login').hidden = true;
  $('#app').hidden = false;
  $('#brand-name').textContent = me.library_name || 'Library';
  $('#who').textContent = me.name;
  document.title = (me.library_name || 'Library') + ' admin';
  route();
}

$('#login-form').onsubmit = async (ev) => {
  ev.preventDefault();
  const f = ev.target;
  $('#login-error').textContent = '';
  try {
    await api('POST', '/api/login', { username: f.username.value, password: f.password.value });
    f.password.value = '';
    showApp(await api('GET', '/api/me'));
  } catch (e) { $('#login-error').textContent = e.message; }
};

$('#logout').onclick = async () => { try { await api('POST', '/api/logout'); } catch (_) {} showLogin(); };
window.addEventListener('hashchange', () => { if (state.me) route(); });

(async () => {
  try { showApp(await api('GET', '/api/me')); } catch (_) { showLogin(); }
})();
