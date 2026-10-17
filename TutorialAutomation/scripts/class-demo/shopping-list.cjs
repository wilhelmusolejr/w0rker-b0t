// Class demo: arrays, objects, and array methods in JavaScript.

const items = [
  { name: "Apples", price: 1.2, quantity: 6 },
  { name: "Bread", price: 2.5, quantity: 1 },
  { name: "Milk", price: 1.8, quantity: 2 },
  { name: "Cheese", price: 4.75, quantity: 1 },
];

function lineTotal(item) {
  return item.price * item.quantity;
}

function receipt(list) {
  const lines = list.map(
    (item) => `${item.name.padEnd(8)} x${item.quantity}  $${lineTotal(item).toFixed(2)}`
  );
  const total = list.reduce((sum, item) => sum + lineTotal(item), 0);
  lines.push(`Total: $${total.toFixed(2)}`);
  return lines.join("\n");
}

const expensive = items.filter((item) => item.price > 2);

console.log(receipt(items));
console.log("Over $2:", expensive.map((item) => item.name).join(", "));
